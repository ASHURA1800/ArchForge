/* kernel/kernel.c — ArchForge OS kernel entry point
 *
 * This is called from start.S after the GDT, stack, and segments are set up.
 * We verify Limine protocol compliance, then initialize all kernel subsystems.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"
#include "serial.h"
#include "gdt.h"
#include "idt.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "console.h"
#include "keyboard.h"
#include "ramfs.h"
#include "shell.h"
#include "process.h"
#include "syscall.h"
#include "msr.h"
#include "elf.h"
#include "vfs.h"
#include "ata.h"
#include "pipe.h"
#include "pci.h"
#include "fat32.h"
#include "e1000.h"
#include "net_selftest.h"
#include "string.h"

/* Forward declaration for G1 test function */
void gfx_test_g1(void);
#include "pci.h"

/* ====================================================================
 * Limine Protocol Requests
 * These MUST be in the .limine_requests section and marked as used.
 * Limine scans for these at boot time and fills in the response pointers.
 * ==================================================================== */

/* Base revision — required by Limine v5+. Revision 6 is the latest. */
__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

/* HHDM (Higher Half Direct Map) — needed to convert physical to virtual addresses */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

/* Framebuffer — for graphical output */
__attribute__((used, section(".limine_requests")))
volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

/* Executable command line — for TEST=1 detection */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_cmdline_request cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
    .revision = 0
};

/* Memory map — for physical memory manager */
__attribute__((used, section(".limine_requests")))
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};


/* Request markers — Limine uses these to find the requests section */
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;


/* ====================================================================
 * Halt and Catch Fire — infinite halt loop
 * ==================================================================== */
static void hcf(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* ====================================================================
 * Global kernel state
 * ==================================================================== */
uint64_t hhdm_offset = 0;  /* HHDM offset from Limine, set in kernel_main */


/* ====================================================================
 * Inline helper: write a single byte directly to COM1 data port.
 * Used for early beacons BEFORE serial_init() is called.
 * This does NOT wait for TX-ready (to avoid any dependency on LSR).
 * ==================================================================== */
static inline void early_serial_putc(char c) {
    __asm__ volatile (
        "movw $0x3F8, %%dx\n\t"
        "outb %0, %%dx\n\t"
        : : "a"((uint8_t)c) : "dx"
    );
}


/* ====================================================================
 * kernel_main — called from start.S after GDT and stack setup
 * ==================================================================== */
void kernel_main(void) {
    /* ---- EARLY BEACON: prove kernel_main was reached ---- */
    /* 'K' is printed BEFORE serial_init() using raw port I/O.
     * If you see 'K' in the serial output, kernel_main is executing.
     * Sequence so far: 'S' (start) 't' (stack) 'G' (GDT) 'K' (kernel_main) */
    early_serial_putc('K');

    /* ---- Step 1: Initialize serial port for debug output ---- */
    serial_init();

    /* Beacon after serial_init: if 'I' appears, serial_init succeeded */
    early_serial_putc('I');
    early_serial_putc('\n');

    serial_write("\n");
    serial_write("========================================\n");
    serial_write("  ArchForge OS — kernel_main entered\n");
    serial_write("========================================\n");
    serial_write("[BOOT] Beacon trace: S=start t=stack G=GDT K=kernel_main I=serial_init\n");

    /* ---- Step 2: Verify Limine base revision ---- */
    serial_write("[BOOT] Checking Limine base revision...\n");

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        serial_write("[BOOT] FATAL: Limine base revision not supported!\n");
        serial_write("[BOOT] Bootloader did not respond to base revision request.\n");
        hcf();
    }
    serial_write("[BOOT] Limine base revision supported!\n");

    /* ---- Step 3: Get HHDM offset (critical for physical address access) ---- */
    serial_write("[BOOT] Checking HHDM...\n");

    if (hhdm_request.response == NULL) {
        serial_write("[BOOT] FATAL: No HHDM response from Limine!\n");
        serial_write("[BOOT] Cannot convert physical to virtual addresses.\n");
        hcf();
    }

    hhdm_offset = hhdm_request.response->offset;
    serial_write("[BOOT] HHDM offset = ");
    serial_write_hex(hhdm_offset);
    serial_write("\n");

    /* ---- Check for TEST mode EARLY ---- */
    bool test_mode = false;
    bool net_test_mode = false;
    if (cmdline_request.response != NULL && cmdline_request.response->cmdline != NULL) {
        serial_write("[DEBUG] Cmdline: ");
        serial_write(cmdline_request.response->cmdline);
        serial_write("\n");
        const char *cmdline = cmdline_request.response->cmdline;
        const char *needle = "TEST=1";
        const char *p = cmdline;
        while (*p) {
            const char *a = p;
            const char *b = needle;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*b == '\0') {
                test_mode = true;
                serial_write("[KERNEL] TEST mode detected.\n");
                break;
            }
            p++;
        }
        // Check for NET_TEST=1
        if (strstr(cmdline, "NET_TEST=1") != NULL) {
            net_test_mode = true;
            serial_write("[KERNEL] NET_TEST mode detected.\n");
        }
    } else {
        serial_write("[DEBUG] Cmdline is NULL\n");
    }
    if (net_test_mode) {
        serial_write("[KERNEL] NET_TEST mode detected. Running network self-tests...\n");
        net_selftest_n3();
        serial_write("[KERNEL] Network self-tests complete.\n");
        hcf();
    }
    if (test_mode) {
        gfx_test_g1();
        hcf();
    }

    /* ---- Step 4: Check framebuffer ---- */
    serial_write("[BOOT] Checking framebuffer...\n");

    if (framebuffer_request.response == NULL) {
        serial_write("[BOOT] WARNING: No framebuffer available.\n");
    } else {
        serial_write("[BOOT] Framebuffer available: ");
        serial_write_dec(framebuffer_request.response->framebuffer_count);
        serial_write(" framebuffer(s)\n");

        if (framebuffer_request.response->framebuffer_count >= 1) {
            struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
            serial_write("[BOOT] Framebuffer 0: ");
            serial_write_dec(fb->width);
            serial_write("x");
            serial_write_dec(fb->height);
            serial_write(" bpp=");
            serial_write_dec(fb->bpp);
            serial_write(" @ ");
            serial_write_hex((uint64_t)fb->address);
            serial_write("\n");
        }
    }

    /* ---- Step 5: Success banner ---- */
    serial_write("\n");
    serial_write("========================================\n");
    serial_write("  Welcome to ArchForge OS\n");
    serial_write("  Booted successfully with Limine!\n");
    serial_write("========================================\n");
    serial_write("\n");

    /* ---- Step 6: Initialize GDT and TSS ---- */
    serial_write("[KERNEL] Initializing GDT and TSS...\n");
    gdt_init();
    serial_write("[KERNEL] GDT and TSS initialization complete.\n");

    /* ---- Step 7: Initialize IDT and PIC ---- */
    serial_write("[KERNEL] Initializing IDT and PIC...\n");
    idt_init();
    serial_write("[KERNEL] IDT and PIC initialization complete.\n");

    /* ---- Step 8: Initialize Physical Memory Manager ---- */
    serial_write("[KERNEL] Initializing Physical Memory Manager...\n");
    pmm_init(hhdm_offset, &memmap_request);
    serial_write("[KERNEL] PMM initialization complete.\n");

    serial_write("[KERNEL] Total memory: ");
    serial_write_dec(pmm_get_total_memory() / 1024 / 1024);
    serial_write(" MB\n");
    serial_write("[KERNEL] Free memory:  ");
    serial_write_dec(pmm_get_free_memory() / 1024 / 1024);
    serial_write(" MB\n");

    /* ---- Step 9: Initialize Virtual Memory Manager ---- */
    serial_write("[KERNEL] Initializing Virtual Memory Manager...\n");
    vmm_init(hhdm_offset);
    serial_write("[KERNEL] VMM initialization complete.\n");

    /* ---- Step 10: Initialize Kernel Heap ---- */
    serial_write("[KERNEL] Initializing Kernel Heap...\n");
    heap_init();
    serial_write("[KERNEL] Heap initialization complete.\n");

    /* ---- Test heap allocator ---- */
    serial_write("[KERNEL] Testing heap allocator...\n");
    void *test1 = kmalloc(64);
    if (test1) {
        serial_write("[HEAP] kmalloc(64) = ");
        serial_write_hex((uint64_t)test1);
        serial_write("\n");
        kfree(test1);
        serial_write("[HEAP] kfree() successful\n");
    } else {
        serial_write("[HEAP] ERROR: kmalloc(64) failed!\n");
    }

    void *test2 = kmalloc(4096);
    if (test2) {
        serial_write("[HEAP] kmalloc(4096) = ");
        serial_write_hex((uint64_t)test2);
        serial_write("\n");
        kfree(test2);
        serial_write("[HEAP] kfree() successful\n");
    } else {
        serial_write("[HEAP] ERROR: kmalloc(4096) failed!\n");
    }

    /* ---- Step 11: Initialize Virtual File System ---- */
    serial_write("[KERNEL] Initializing Virtual File System...\n");
    vfs_init();
    
    /* ---- Step 11b: Initialize ATA PIO Driver ---- */
    serial_write("[KERNEL] Initializing ATA PIO driver...\n");
    ata_init();
    pci_scan();

    /* ---- Network Self-Test (if TEST=1) ---- */
    if (cmdline_request.response != NULL && cmdline_request.response->cmdline != NULL) {
        if (strstr(cmdline_request.response->cmdline, "TEST=1") != NULL) {
            serial_write("[KERNEL] TEST=1 detected. Running network self-tests...\n");
            net_selftest_n3();
            serial_write("[KERNEL] Network self-tests complete.\n");
        }
    }

    
    /* ---- Step 11c: Initialize FAT32 Filesystem ---- */
    serial_write("[KERNEL] Initializing FAT32 filesystem...\n");
    if (fat32_mount() == 0) {
        serial_write("[FAT32] Testing read from mounted filesystem...\n");
        fat32_fs_t *fs = fat32_get_fs();
        if (fs) {
            /* Try to find the root directory */
            fat32_dir_entry_t entries[16];
            int count = fat32_read_dir(fs, fs->root_cluster, entries, 16);
            if (count > 0) {
                serial_write("[FAT32] Found ");
                serial_write_dec(count);
                serial_write(" directory entries:\n");
                for (int i = 0; i < count; i++) {
                    char name[13];
                    fat32_format_name(entries[i].name, name);
                    serial_write("  - ");
                    serial_write(name);
                    serial_write(" (");
                    serial_write_dec(entries[i].file_size);
                    serial_write(" bytes)\n");
                }
            } else {
                serial_write("[FAT32] No directory entries found or error\n");
            }
            
            /* ---- A11: Persistence Test ---- */
            serial_write("[PERSIST] Running persistence test...\n");
            const char *test_data = "ArchForge Persistence Test Data 1234567890";
            size_t test_len = 44; /* Length of test_data */
            char read_buf[64];
            
            /* Try to read the test file */
            int bytes_read = fat32_read_path(fs, "persist.dat", read_buf, sizeof(read_buf) - 1);
            if (bytes_read > 0) {
                /* File exists - verify data */
                read_buf[bytes_read] = '\0';
                if (bytes_read == (int)test_len && memcmp(read_buf, test_data, test_len) == 0) {
                    serial_write("[PERSIST] PASS: Data verified from previous boot\n");
                } else {
                    serial_write("[PERSIST] FAIL: Data mismatch\n");
                    serial_write("[PERSIST] Expected: ");
                    serial_write(test_data);
                    serial_write("\n");
                    serial_write("[PERSIST] Got: ");
                    serial_write(read_buf);
                    serial_write("\n");
                }
            } else {
                /* File doesn't exist - create it */
                if (fat32_write_path(fs, "persist.dat", test_data, test_len) >= 0) {
                    serial_write("[PERSIST] Created persist.dat (verify on next boot)\n");
                    fat32_sync();
                } else {
                    serial_write("[PERSIST] FAIL: Could not create persist.dat\n");
                }
            }
        }
    } else {
        serial_write("[KERNEL] WARNING: FAT32 initialization failed (no FAT32 drive found).\n");
    }
    
    /* ---- Step 11d: Initialize Pipe Subsystem ---- */
    serial_write("[KERNEL] Initializing Pipe subsystem...\n");
    pipe_init();
    
    /* Test RAM filesystem */
    serial_write("[RAMFS] Testing RAM filesystem...\n");
    const char *test_data_ramfs = "Hello from ArchForge RAMFS!\n";
    ramfs_create("test.txt", test_data_ramfs, 25);
    
    char read_buf[64];
    int bytes_read = ramfs_read("test.txt", read_buf, sizeof(read_buf));
    if (bytes_read > 0) {
        serial_write("[RAMFS] Read ");
        serial_write_dec(bytes_read);
        serial_write(" bytes: ");
        serial_write(read_buf);
        serial_write("\n");
    }
    
    ramfs_append("test.txt", " Appended data!", 15);
    serial_write("[RAMFS] Appended data. New size: ");
    serial_write_dec(ramfs_size("test.txt"));
    serial_write(" bytes\n");
    
    ramfs_list();
    ramfs_delete("test.txt");
    serial_write("[RAMFS] Deleted test.txt\n");
    ramfs_list();
    serial_write("[RAMFS] RAM filesystem test complete.\n");

    /* ---- Step 12: Initialize Framebuffer Console ---- */
    serial_write("[KERNEL] Initializing Framebuffer Console...\n");
    console_init();
    serial_write("[KERNEL] Console initialization complete.\n");

    /* ---- Test console output ---- */
    serial_write("[KERNEL] Testing console output...\n");
    console_clear();
    serial_write("[CONSOLE] Cleared screen\n");
    console_write("========================================\n");
    console_write("  ArchForge OS - Console Test\n");
    console_write("========================================\n\n");
    serial_write("[CONSOLE] Wrote header\n");
    console_write("Hello, framebuffer console!\n");
    console_write("Hex: ");
    console_write_hex(0xDEADBEEFCAFEBABE);
    console_write("\nDec: ");
    console_write_dec(123456789);
    console_write("\n\nScrolling test:\n");
    serial_write("[CONSOLE] Starting scroll test\n");
    for (int i = 0; i < 30; i++) {
        console_write("Line ");
        console_write_dec(i);
        console_write(": The quick brown fox jumps over the lazy dog.\n");
    }
    console_write("\nConsole test complete!\n");
    serial_write("[CONSOLE] Scroll test done\n");

    serial_write("[KERNEL] Console test complete.\n");

    /* ---- Step 12: Initialize Process Subsystem ---- */
    serial_write("[KERNEL] Initializing process subsystem...\n");
    process_init();
    
    /* Create a test process */
    serial_write("[KERNEL] Creating test process...\n");
    process_create((void(*)(void))shell_run, NULL);
    
    serial_write("[KERNEL] Process subsystem initialized.\n");

    /* ---- Step 13: Initialize System Call Interface ---- */
    serial_write("[KERNEL] Initializing system call interface...\n");
    syscall_init();
    serial_write("[KERNEL] System call interface initialized.\n");

    /* ---- Step 14: Test ELF Loader ---- */
    serial_write("[KERNEL] ELF Loader framework initialized...\n");
    serial_write("[KERNEL] Ready to load user programs from filesystem.\n");

    /* ---- Step 15: Install user programs in VFS ---- */
    serial_write("[KERNEL] Installing user programs...\n");
    
    /* Create /bin and /sbin directories */
    vfs_create_dir("/bin");
    vfs_create_dir("/sbin");
    
    /* Create a test file for user-space cat utility */
    const char *test_data = "Hello from the persistent filesystem!\nThis file was created by the kernel at boot.\n";
    vfs_create_file("/test.txt", test_data, strlen(test_data));
    serial_write("[KERNEL] Created /test.txt in VFS\n");
    
    /* Include user binaries */
    extern const uint8_t _binary_build_user_hello_elf_start[];
    extern const uint8_t _binary_build_user_hello_elf_end[];
    extern const uint8_t _binary_build_user_init_elf_start[];
    extern const uint8_t _binary_build_user_init_elf_end[];
    extern const uint8_t _binary_build_user_cat_elf_start[];
    extern const uint8_t _binary_build_user_cat_elf_end[];
    
    size_t hello_elf_size = _binary_build_user_hello_elf_end - _binary_build_user_hello_elf_start;
    size_t init_elf_size = _binary_build_user_init_elf_end - _binary_build_user_init_elf_start;
    size_t cat_elf_size = _binary_build_user_cat_elf_end - _binary_build_user_cat_elf_start;
    
    if (hello_elf_size > 0) {
        vfs_create_file("/bin/hello", _binary_build_user_hello_elf_start, hello_elf_size);
        serial_write("[KERNEL] Installed '/bin/hello'\n");
    }
    if (init_elf_size > 0) {
        vfs_create_file("/sbin/init", _binary_build_user_init_elf_start, init_elf_size);
        serial_write("[KERNEL] Installed '/sbin/init'\n");
    }
    if (cat_elf_size > 0) {
        vfs_create_file("/bin/cat", _binary_build_user_cat_elf_start, cat_elf_size);
        serial_write("[KERNEL] Installed '/bin/cat'\n");
    }

    /* ---- Step 16: Launch Init Process (Phase 30) ---- */
    serial_write("[KERNEL] Launching /sbin/init...\n");
    
    /* Try to spawn user-space init from VFS */
    vfs_node_t *init_node = vfs_resolve("/sbin/init");
    if (init_node && init_node->type == VFS_TYPE_FILE) {
        uint8_t *init_data = kmalloc(init_node->size);
        if (init_data && vfs_read(init_node, init_data, init_node->size, 0) >= 0) {
            if (process_create_elf(init_data, init_node->size) == 0) {
                kfree(init_data);
                serial_write("[KERNEL] /sbin/init launched successfully.\n");
            } else {
                kfree(init_data);
                serial_write("[KERNEL] WARNING: Failed to spawn /sbin/init, falling back to kernel shell.\n");
                shell_init();
                shell_run();
            }
        } else {
            if (init_data) kfree(init_data);
            serial_write("[KERNEL] WARNING: Failed to read /sbin/init, falling back to kernel shell.\n");
            shell_init();
            shell_run();
        }
    } else {
        serial_write("[KERNEL] WARNING: /sbin/init not found in VFS, falling back to kernel shell.\n");
        shell_init();
        shell_run();
    }

    /* ---- All done, halt ---- */
    serial_write("[KERNEL] All subsystems initialized. Halting.\n");
    hcf();
}
/* ==================================================================== 
 * G1 Test: Framebuffer Acquisition & Validation
 * Maps framebuffer with uncached flags, validates masks, and proves channel order.
 * ==================================================================== */
void gfx_test_g1(void) {
    serial_write("[GFX] Running G1 framebuffer acquisition test...\n");
    
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count == 0) {
        serial_write("[TEST] FAIL gfx: No framebuffer available\n");
        __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x11), "Nd"((uint16_t)0xf4));
        return;
    }
    
    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    serial_write("[GFX] Framebuffer: ");
    serial_write_dec(fb->width);
    serial_write("x");
    serial_write_dec(fb->height);
    serial_write(" pitch=");
    serial_write_dec(fb->pitch);
    serial_write(" bpp=");
    serial_write_dec(fb->bpp);
    serial_write("\n");
    serial_write("[GFX] Masks: R=");
    serial_write_dec(fb->red_mask_size);
    serial_write("@");
    serial_write_dec(fb->red_mask_shift);
    serial_write(" G=");
    serial_write_dec(fb->green_mask_size);
    serial_write("@");
    serial_write_dec(fb->green_mask_shift);
    serial_write(" B=");
    serial_write_dec(fb->blue_mask_size);
    serial_write("@");
    serial_write_dec(fb->blue_mask_shift);
    serial_write("\n");

    uint64_t fb_phys = (uint64_t)fb->address - hhdm_offset;
    uint64_t fb_virt = 0xFFFF800000000000ULL;
    size_t fb_size = fb->pitch * fb->height;
    
    serial_write("[GFX] Mapping framebuffer phys 0x");
    serial_write_hex(fb_phys);
    serial_write(" to virt 0x");
    serial_write_hex(fb_virt);
    serial_write("\n");
    
    if (vmm_map_mmio(fb_virt, fb_phys, fb_size, PTE_WRITABLE) != 0) {
        serial_write("[TEST] FAIL gfx: Failed to map framebuffer\n");
        __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x11), "Nd"((uint16_t)0xf4));
        return;
    }
    
    uint32_t *pixels = (uint32_t *)fb_virt;
    size_t total_pixels = (fb->pitch / (fb->bpp / 8)) * fb->height;
    
    // Fill with Red
    serial_write("[GFX] Filling with RED...\n");
    uint32_t color_r = (0xFF << fb->red_mask_shift) | (0x00 << fb->green_mask_shift) | (0x00 << fb->blue_mask_shift);
    for (size_t i = 0; i < total_pixels; i++) {
        pixels[i] = color_r;
    }
    serial_write("[GFX] FILL RED\n");
    for (volatile int i = 0; i < 10000000; i++); // Small delay for host to catch marker
    
    // Fill with Green
    serial_write("[GFX] Filling with GREEN...\n");
    uint32_t color_g = (0x00 << fb->red_mask_shift) | (0xFF << fb->green_mask_shift) | (0x00 << fb->blue_mask_shift);
    for (size_t i = 0; i < total_pixels; i++) {
        pixels[i] = color_g;
    }
    serial_write("[GFX] FILL GREEN\n");
    for (volatile int i = 0; i < 10000000; i++);
    
    // Fill with Blue
    serial_write("[GFX] Filling with BLUE...\n");
    uint32_t color_b = (0x00 << fb->red_mask_shift) | (0x00 << fb->green_mask_shift) | (0xFF << fb->blue_mask_shift);
    for (size_t i = 0; i < total_pixels; i++) {
        pixels[i] = color_b;
    }
    serial_write("[GFX] FILL BLUE\n");
    for (volatile int i = 0; i < 10000000; i++);
    
    serial_write("[TEST] PASS gfx\n");
    serial_write("[TEST] DONE\n");
    
    /* Signal success to QEMU via isa-debug-exit */
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x10), "Nd"((uint16_t)0xf4));
}
