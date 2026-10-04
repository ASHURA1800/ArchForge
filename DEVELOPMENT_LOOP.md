# 🚀 ArchForge OS — Complete Autonomous Development Loop

## META-INSTRUCTIONS FOR AI ASSISTANT

You are an expert bare-metal x86_64 OS engineer. You have full autonomy to read,
modify, build, and test the ArchForge OS project at `/home/sagar/Projects/ArchForge`.
Use the **Filesystem MCP** for all file operations. Use **Firecrawl MCP** when you
need to verify current Limine/x86_64 documentation.

### STRICT PROTOCOL
1. **Read before edit**: NEVER assume file contents. Always read first.
2. **Minimal changes**: One hypothesis, one fix per iteration.
3. **Build and test after EVERY change**: Verify before proceeding.
4. **Capture evidence**: Copy exact error messages and serial output.
5. **No hallucination**: If unsure about x86_64/Limine details, use Firecrawl.
6. **Iterate until success**: Never stop until the current phase passes.

---

## 📋 CURRENT STATE (as of latest session)

### Bootloader
- **Limine v12.6.1** downloaded in `build/limine/`
- `limine-deploy` binary at `build/limine/bin/limine-deploy`
- ISO built with proper xorriso flags (hybrid BIOS+UEFI)
- HDD image (64MB FAT32) built as reliable fallback
- `boot/limine.conf` has `serial: yes` for Limine debug output

### Kernel Source
- `kernel/start.S` — Entry point with beacons 'S', 't', 'G'
- `kernel/kernel.c` — Main entry, Limine requests, subsystem init (with beacons 'K', 'I')
- `kernel/serial.c` — COM1 (0x3F8) driver, 38400 baud, 8N1
- `kernel/gdt.c` + `kernel/gdt_asm.S` — 7-entry GDT with TSS
- `kernel/idt.c` + `kernel/isrs.S` — IDT with PIC remapped (IRQ0/IRQ1)
- `kernel/interrupt_handlers.c` — Exception dump + IRQ EOI
- `kernel/pmm.c` — Bitmap physical page allocator using HHDM
- `kernel/linker.ld` — Higher-half at 0xffffffff80000000 with PHDRs
- `include/limine.h` — Official Limine protocol header
- `include/stdint.h`, `include/stdbool.h`, `include/stddef.h` — C types
- `include/io.h` — Inline port I/O (inb/outb)

### Build Commands
```bash
cd /home/sagar/Projects/ArchForge
make clean && make       # Full rebuild
make run                 # BIOS ISO in QEMU
make run-uefi            # UEFI ISO in QEMU (needs OVMF)
make run-hdd             # BIOS HDD image in QEMU (most reliable)
make verify-iso          # Inspect ISO structure
make debug               # GDB server on port 1234
```

---

## 🔧 PHASE 0: VERIFY CURRENT STATE

**Before making ANY changes**, run this verification sequence:

### Step 0.1: Clean Build
```bash
cd /home/sagar/Projects/ArchForge
make clean
make
```
**PASS**: Build succeeds with no warnings or errors.
**FAIL**: Read error, identify failing file, apply minimal fix, retry.

### Step 0.2: Verify Kernel Structure
```bash
readelf -h build/kernel.elf | grep "Entry point"
readelf -S build/kernel.elf | grep limine
readelf -l build/kernel.elf | head -30
```
**PASS**:
- Entry point = `_start` symbol address
- `.limine_requests` section exists at 0xffffffff80000000
- PT_LOAD segments have correct alignments

### Step 0.3: Test HDD Boot (most reliable)
```bash
timeout 15 qemu-system-x86_64 \
    -M q35 -m 2G \
    -hda build/archforge.img \
    -serial stdio -display none \
    -no-reboot -no-shutdown
```

**Expected Serial Output** (in this exact order):
```
StGKI

========================================
  ArchForge OS — kernel_main entered
========================================
[BOOT] Beacon trace: S=start t=stack G=GDT K=kernel_main I=serial_init
[BOOT] Checking Limine base revision...
[BOOT] Limine base revision supported!
[BOOT] Checking HHDM...
[BOOT] HHDM offset = 0xffff800000000000
[BOOT] Checking framebuffer...
[BOOT] Framebuffer available: 1 framebuffer(s)
...
[KERNEL] Initializing GDT and TSS...
[GDT] Initializing Global Descriptor Table...
[GDT] GDT loaded successfully.
[GDT] TSS loaded successfully.
[KERNEL] GDT and TSS initialization complete.
[KERNEL] Initializing IDT and PIC...
[IDT] Remapping PIC...
[IDT] PIC remapped to 0x20-0x2F.
[IDT] IDT loaded successfully.
[IDT] Timer and Keyboard interrupts enabled.
[KERNEL] IDT and PIC initialization complete.
[KERNEL] Initializing Physical Memory Manager...
[PMM] ... (memory map entries)
[PMM] PMM initialized successfully.
[KERNEL] PMM initialization complete.
[KERNEL] Total memory: XXX MB
[KERNEL] Free memory:  XXX MB
[KERNEL] All subsystems initialized. Halting.
```

### Step 0.4: Diagnose Failures (Decision Tree)

Use this beacon trace to pinpoint failures:

| Beacons Seen | Diagnosis | Action |
|--------------|-----------|--------|
| Nothing | Limine not loading kernel or QEMU serial not connected | Check `make verify-iso`, try `-serial mon:stdio` |
| `S` only | Stack setup fails | Inspect `start.S` for stack overflow or alignment |
| `St` only | GDT load or far-jump fails | Check GDT selectors match CS=0x08, DS=0x10 |
| `StG` only | `kernel_main` not called | Stack alignment (RSP mod 16), segment registers |
| `StGK` only | `serial_init` breaks port | Review `serial.c` register writes |
| `StGKI` but no further | Crash in Limine checks | Verify request IDs in `limine.h` match protocol |
| All up to GDT | GDT/TSS init crash | Check `gdt.c` TSS base address alignment |
| All up to IDT | IDT/PIC init crash | Check PIC ports 0x20/0xA0 |
| All up to PMM | PMM init crash | Verify HHDM offset used correctly |

### Step 0.5: Test UEFI Boot (secondary)
```bash
OVMF=$(ls /usr/share/OVMF/OVMF_CODE.fd /usr/share/edk2/x64/OVMF_CODE.fd 2>/dev/null | head -1)
timeout 20 qemu-system-x86_64 \
    -M q35 -m 2G -bios "$OVMF" \
    -cdrom build/archforge.iso \
    -serial stdio -display none -no-reboot
```

**PASS**: Same welcome message appears.
**FAIL**: Add `-d guest_errors -D /tmp/qemu.log` and inspect.

---

## 🔄 PHASE 1: VIRTUAL MEMORY MANAGER (VMM)

### Goal
Implement 4-level page table management (PML4 → PDPT → PD → PT) to enable
virtual memory, identity mapping, and higher-half kernel mapping.

### Architectural Context
x86_64 uses a 4-level page table hierarchy:
- **PML4** (Page Map Level 4): 512 entries, 8 bytes each = 4KB
- **PDPT** (Page Directory Pointer Table): 512 entries
- **PD** (Page Directory): 512 entries
- **PT** (Page Table): 512 entries → maps to 4KB physical page

Each entry is 64 bits with flags in lower 12 bits and physical address in bits 12-51.

### Step 1.1: Create `include/vmm.h`
```c
#ifndef ARCHFORGE_VMM_H
#define ARCHFORGE_VMM_H

#include <stdint.h>
#include <stdbool.h>

/* Page Table Entry flags (bits 0-11) */
#define PTE_PRESENT    (1ULL << 0)   /* Page is present in memory */
#define PTE_WRITABLE   (1ULL << 1)   /* Page is writable */
#define PTE_USER       (1ULL << 2)   /* Page accessible from ring 3 */
#define PTE_PWT        (1ULL << 3)   /* Page-level write-through */
#define PTE_PCD        (1ULL << 4)   /* Page-level cache disable */
#define PTE_ACCESSED   (1ULL << 5)   /* CPU sets this on access */
#define PTE_DIRTY      (1ULL << 6)   /* CPU sets this on write */
#define PTE_HUGE       (1ULL << 7)   /* 2MB/1GB page (PD/PDPT level) */
#define PTE_GLOBAL     (1ULL << 8)   /* Global page (not flushed on CR3 write) */
#define PTE_NX         (1ULL << 63)  /* No-execute (requires NXE bit in IA32_EFER) */

/* Address mask: bits 12-51 contain the physical address */
#define PTE_ADDR_MASK  0x000FFFFFFFFFF000ULL

/* Initialize VMM using Limine's existing page tables */
void vmm_init(uint64_t hhdm_offset);

/* Map a virtual page to a physical page with given flags */
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);

/* Unmap a virtual page */
int vmm_unmap_page(uint64_t virt);

/* Get the physical address mapped to a virtual address (0 if not mapped) */
uint64_t vmm_get_physical(uint64_t virt);

/* Invalidate TLB entry for a virtual address */
static inline void invlpg(uint64_t addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

/* Flush entire TLB by reloading CR3 */
static inline void flush_tlb(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

#endif /* ARCHFORGE_VMM_H */
```

### Step 1.2: Create `kernel/vmm.c`
```c
#include "vmm.h"
#include "pmm.h"
#include "serial.h"

static uint64_t *kernel_pml4 = NULL;  /* Virtual address of current PML4 */
static uint64_t hhdm_offset = 0;

/* Convert physical address to virtual using HHDM */
static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + hhdm_offset);
}

/* Convert virtual address to physical (only for HHDM-mapped addresses) */
static inline uint64_t virt_to_phys(void *virt) {
    return (uint64_t)virt - hhdm_offset;
}

/* Get or allocate the next-level table for a given entry */
static uint64_t *get_or_alloc_table(uint64_t *entry) {
    if (*entry & PTE_PRESENT) {
        /* Table already exists — return its virtual address */
        uint64_t phys = *entry & PTE_ADDR_MASK;
        return (uint64_t *)phys_to_virt(phys);
    }

    /* Allocate a new page for the table */
    void *new_page = pmm_alloc(1);
    if (!new_page) {
        serial_write("[VMM] FATAL: Out of memory allocating page table!\n");
        return NULL;
    }

    /* Zero the new table */
    uint64_t *table = (uint64_t *)phys_to_virt((uint64_t)new_page);
    for (int i = 0; i < 512; i++) {
        table[i] = 0;
    }

    /* Install the entry: present + writable + user (tables must be accessible) */
    *entry = (uint64_t)new_page | PTE_PRESENT | PTE_WRITABLE | PTE_USER;

    return table;
}

/* ====================================================================
 * vmm_init — Initialize VMM using Limine's existing page tables
 * ==================================================================== */
void vmm_init(uint64_t hhdm_off) {
    serial_write("[VMM] Initializing Virtual Memory Manager...\n");
    hhdm_offset = hhdm_off;

    /* Read current CR3 (PML4 physical address) */
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    kernel_pml4 = (uint64_t *)phys_to_virt(cr3 & PTE_ADDR_MASK);

    serial_write("[VMM] Using Limine's PML4 at phys ");
    serial_write_hex(cr3 & PTE_ADDR_MASK);
    serial_write(" (virt ");
    serial_write_hex((uint64_t)kernel_pml4);
    serial_write(")\n");

    /* Verify identity mapping: virt 0x1000 should map to phys 0x1000 */
    /* (Limine should have set this up) */

    /* Test: map a scratch page */
    void *test_phys = pmm_alloc(1);
    if (test_phys) {
        uint64_t test_virt = 0xffff900000000000ULL;  /* Scratch area */
        if (vmm_map_page(test_virt, (uint64_t)test_phys,
                        PTE_PRESENT | PTE_WRITABLE) == 0) {
            /* Write and read back to verify */
            volatile uint64_t *ptr = (volatile uint64_t *)test_virt;
            *ptr = 0xDEADBEEFCAFEBABEULL;
            if (*ptr == 0xDEADBEEFCAFEBABEULL) {
                serial_write("[VMM] Test mapping successful!\n");
            } else {
                serial_write("[VMM] WARNING: Test mapping failed!\n");
            }
            vmm_unmap_page(test_virt);
            pmm_free(test_phys, 1);
        }
    }

    serial_write("[VMM] VMM initialized successfully.\n");
}

/* ====================================================================
 * vmm_map_page — Map virtual page to physical page
 * ==================================================================== */
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    /* Walk/create page table hierarchy */
    uint64_t *pdpt = get_or_alloc_table(&kernel_pml4[pml4_idx]);
    if (!pdpt) return -1;

    uint64_t *pd = get_or_alloc_table(&pdpt[pdpt_idx]);
    if (!pd) return -1;

    uint64_t *pt = get_or_alloc_table(&pd[pd_idx]);
    if (!pt) return -1;

    /* Install the leaf PTE */
    pt[pt_idx] = (phys & PTE_ADDR_MASK) | (flags & ~PTE_ADDR_MASK);

    /* Invalidate TLB for this page */
    invlpg(virt);

    return 0;
}

/* ====================================================================
 * vmm_unmap_page — Remove mapping for a virtual page
 * ==================================================================== */
int vmm_unmap_page(uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(kernel_pml4[pml4_idx] & PTE_PRESENT)) return -1;
    uint64_t *pdpt = phys_to_virt(kernel_pml4[pml4_idx] & PTE_ADDR_MASK);

    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) return -1;
    uint64_t *pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    if (!(pd[pd_idx] & PTE_PRESENT)) return -1;
    uint64_t *pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);

    /* Clear the PTE */
    pt[pt_idx] = 0;

    /* Invalidate TLB */
    invlpg(virt);

    return 0;
}

/* ====================================================================
 * vmm_get_physical — Walk page tables to find physical mapping
 * ==================================================================== */
uint64_t vmm_get_physical(uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(kernel_pml4[pml4_idx] & PTE_PRESENT)) return 0;
    uint64_t *pdpt = phys_to_virt(kernel_pml4[pml4_idx] & PTE_ADDR_MASK);

    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) return 0;
    uint64_t *pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    /* Check for 2MB huge page */
    if (pd[pd_idx] & PTE_HUGE) {
        return (pd[pd_idx] & 0x000FFFFFFFE00000ULL) + (virt & 0x1FFFFF);
    }

    if (!(pd[pd_idx] & PTE_PRESENT)) return 0;
    uint64_t *pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);

    if (!(pt[pt_idx] & PTE_PRESENT)) return 0;
    return (pt[pt_idx] & PTE_ADDR_MASK) + (virt & 0xFFF);
}
```

### Step 1.3: Integrate into `kernel/kernel.c`
Add after PMM init:
```c
#include "vmm.h"
...
serial_write("[KERNEL] Initializing Virtual Memory Manager...\n");
vmm_init(hhdm_offset);
serial_write("[KERNEL] VMM initialization complete.\n");
```

### Step 1.4: Update Makefile
Add `vmm.o` to KERNEL_ELF dependencies:
```makefile
$(KERNEL_ELF): ... $(BUILD_DIR)/vmm.o
```

Add pattern rule (already exists for .c files, so vmm.c will auto-compile).

### Step 1.5: Build and Test
```bash
make clean && make
make run-hdd
```

**PASS**: Serial shows "[VMM] Test mapping successful!" and all subsystems initialize.
**FAIL**: Read error, fix, retry.

---

## 🔄 PHASE 2: KERNEL HEAP ALLOCATOR

### Goal
Implement `kmalloc`/`kfree` for dynamic memory allocation on top of PMM + VMM.

### Step 2.1: Create `include/heap.h`
```c
#ifndef ARCHFORGE_HEAP_H
#define ARCHFORGE_HEAP_H

#include <stdint.h>
#include <stddef.h>

/* Kernel heap virtual address range */
#define HEAP_BASE   0xffffa00000000000ULL
#define HEAP_SIZE   (16 * 1024 * 1024)  /* 16 MB initial heap */

/* Initialize the kernel heap */
void heap_init(void);

/* Allocate 'size' bytes from the kernel heap */
void *kmalloc(size_t size);

/* Allocate zeroed memory */
void *kcalloc(size_t count, size_t size);

/* Reallocate memory */
void *krealloc(void *ptr, size_t new_size);

/* Free previously allocated memory */
void kfree(void *ptr);

/* Get heap statistics */
void heap_stats(uint64_t *total, uint64_t *used, uint64_t *free_mem);

#endif /* ARCHFORGE_HEAP_H */
```

### Step 2.2: Create `kernel/heap.c`
```c
#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "../include/stdbool.h"

/* Block header placed before each allocation */
typedef struct block_header {
    size_t size;                /* Size of this block (including header) */
    bool is_free;
    struct block_header *next;  /* Next block in the list */
} block_header_t;

#define HEADER_SIZE sizeof(block_header_t)
#define MIN_BLOCK_SIZE (HEADER_SIZE + 16)  /* Minimum allocation unit */

static block_header_t *heap_start = NULL;
static uint64_t heap_total = 0;
static uint64_t heap_used = 0;

/* ====================================================================
 * heap_init — Initialize kernel heap
 * ==================================================================== */
void heap_init(void) {
    serial_write("[HEAP] Initializing kernel heap at ");
    serial_write_hex(HEAP_BASE);
    serial_write(" (");
    serial_write_dec(HEAP_SIZE / 1024 / 1024);
    serial_write(" MB)\n");

    /* Allocate physical pages and map them to the heap region */
    size_t pages_needed = HEAP_SIZE / 4096;
    for (size_t i = 0; i < pages_needed; i++) {
        void *phys = pmm_alloc(1);
        if (!phys) {
            serial_write("[HEAP] FATAL: Out of physical memory for heap!\n");
            return;
        }
        uint64_t virt = HEAP_BASE + (i * 4096);
        if (vmm_map_page(virt, (uint64_t)phys,
                        PTE_PRESENT | PTE_WRITABLE) != 0) {
            serial_write("[HEAP] FATAL: Failed to map heap page!\n");
            return;
        }
    }

    /* Initialize the first (and only) free block */
    heap_start = (block_header_t *)HEAP_BASE;
    heap_start->size = HEAP_SIZE;
    heap_start->is_free = true;
    heap_start->next = NULL;

    heap_total = HEAP_SIZE;
    heap_used = 0;

    serial_write("[HEAP] Heap initialized successfully.\n");
}

/* ====================================================================
 * kmalloc — Allocate memory from the heap (first-fit)
 * ==================================================================== */
void *kmalloc(size_t size) {
    if (size == 0 || heap_start == NULL) return NULL;

    /* Round up to 16-byte alignment and add header */
    size_t total_size = ((size + HEADER_SIZE + 15) / 16) * 16;
    if (total_size < MIN_BLOCK_SIZE) total_size = MIN_BLOCK_SIZE;

    /* First-fit search */
    block_header_t *current = heap_start;
    while (current != NULL) {
        if (current->is_free && current->size >= total_size) {
            /* Found a suitable block */

            /* Split if the remaining space is large enough */
            if (current->size >= total_size + MIN_BLOCK_SIZE) {
                block_header_t *new_block = (block_header_t *)((char *)current + total_size);
                new_block->size = current->size - total_size;
                new_block->is_free = true;
                new_block->next = current->next;

                current->size = total_size;
                current->next = new_block;
            }

            current->is_free = false;
            heap_used += current->size;

            /* Return pointer past the header */
            return (void *)((char *)current + HEADER_SIZE);
        }
        current = current->next;
    }

    serial_write("[HEAP] WARNING: kmalloc(");
    serial_write_dec(size);
    serial_write(") failed — out of heap space!\n");
    return NULL;
}

/* ====================================================================
 * kfree — Free allocated memory
 * ==================================================================== */
void kfree(void *ptr) {
    if (ptr == NULL) return;

    /* Get the block header */
    block_header_t *block = (block_header_t *)((char *)ptr - HEADER_SIZE);
    block->is_free = true;
    heap_used -= block->size;

    /* Coalesce with adjacent free blocks */
    block_header_t *current = heap_start;
    while (current != NULL) {
        if (current->is_free && current->next != NULL && current->next->is_free) {
            /* Merge current with next */
            current->size += current->next->size;
            current->next = current->next->next;
            continue;  /* Check again in case of multiple merges */
        }
        current = current->next;
    }
}

/* ====================================================================
 * kcalloc — Allocate zeroed memory
 * ==================================================================== */
void *kcalloc(size_t count, size_t size) {
    size_t total = count * size;
    void *ptr = kmalloc(total);
    if (ptr) {
        uint8_t *p = (uint8_t *)ptr;
        for (size_t i = 0; i < total; i++) p[i] = 0;
    }
    return ptr;
}

/* ====================================================================
 * krealloc — Reallocate memory
 * ==================================================================== */
void *krealloc(void *ptr, size_t new_size) {
    if (ptr == NULL) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return NULL; }

    block_header_t *block = (block_header_t *)((char *)ptr - HEADER_SIZE);
    size_t old_data_size = block->size - HEADER_SIZE;

    if (new_size <= old_data_size) return ptr;  /* Shrinking: no-op */

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;

    /* Copy old data */
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (size_t i = 0; i < old_data_size; i++) dst[i] = src[i];

    kfree(ptr);
    return new_ptr;
}

/* ====================================================================
 * heap_stats — Report heap statistics
 * ==================================================================== */
void heap_stats(uint64_t *total, uint64_t *used, uint64_t *free_mem) {
    if (total) *total = heap_total;
    if (used) *used = heap_used;
    if (free_mem) *free_mem = heap_total - heap_used;
}
```

### Step 2.3: Integrate
- Add `heap.o` to Makefile
- Call `heap_init()` after `vmm_init()` in kernel.c
- Test: allocate various sizes, free some, reallocate

---

## 🔄 PHASE 3: KERNEL PRINTF (kprintf)

### Goal
Implement formatted printing for the kernel console.

### Implementation
- Parse format specifiers: `%d`, `%u`, `%x`, `%X`, `%p`, `%s`, `%c`, `%ld`, `%lu`, `%lx`, `%%`
- Handle width, precision, flags (`0`, `-`, `+`, ` `)
- Output to both serial and (later) framebuffer

### Integration
- Create `include/kprintf.h` and `kernel/kprintf.c`
- Replace verbose `serial_write` calls with `kprintf`

---

## 🔄 PHASE 4: PS/2 KEYBOARD DRIVER

### Goal
Read keyboard input via IRQ1 and convert scancodes to ASCII.

### Implementation
- Scancode Set 1 translation table
- Ring buffer for key events
- Shift/Ctrl/Alt modifier handling
- `keyboard_read()` blocking function

---

## 🔄 PHASE 5: VGA TEXT MODE CONSOLE

### Goal
Display text on screen using VGA text mode at 0xB8000.

### Implementation
- 80x25 character grid
- Scrolling
- Cursor management
- Color attributes

---

## 🔄 PHASE 6: KERNEL SHELL

### Goal
Implement a command-line interpreter.

### Built-in Commands
- `help` — list commands
- `clear` — clear screen
- `mem` — memory statistics
- `ps` — list processes (placeholder)
- `echo <text>` — print text
- `time` — show uptime
- `reboot` / `halt`

---

## 🔄 PHASE 7: SYSTEM TIMER (PIT)

### Goal
Implement a programmable interval timer for timekeeping.

### Implementation
- PIT channel 0, mode 3 (square wave)
- 1000 Hz (1ms ticks)
- `timer_get_ms()`, `timer_sleep_ms()`

---

## 🔄 PHASE 8: CONTEXT SWITCHING & PROCESSES

### Goal
Implement preemptive multitasking with round-robin scheduling.

### Implementation
- Process Control Block (PCB)
- Context switch in assembly (`switch.S`)
- Timer-driven scheduler
- `process_yield()`, `process_exit()`

---

## 🔄 PHASE 9: SYSTEM CALLS

### Goal
Implement a syscall interface for user-mode processes.

### Implementation
- `syscall`/`sysret` instructions via MSR
- Syscall numbers: exit, write, read, yield, getpid, mmap
- Ring 3 ↔ Ring 0 transitions

---

## 🔄 PHASE 10: RAMDISK FILESYSTEM

### Goal
In-memory filesystem for boot-time files.

### Implementation
- File table with name/data/size
- `ramfs_create`, `ramfs_read`, `ramfs_write`, `ramfs_delete`
- Shell commands: `ls`, `cat`, `write`, `rm`

---

## 🔄 PHASE 11: VFS LAYER

### Goal
Abstract filesystem operations for multiple backends.

### Implementation
- `vfs_node_t` tree structure
- Path parsing and traversal
- Mount points
- Operations table (read, write, readdir, finddir)

---

## 🔄 PHASE 12: AHCI STORAGE DRIVER

### Goal
Read/write sectors from SATA disks via AHCI.

### Implementation
- PCI scan for AHCI controller
- HBA memory-mapped registers
- Port initialization and command submission
- DMA for data transfer

---

## 🔄 PHASE 13: EXT2 FILESYSTEM

### Goal
Read files from ext2 partitions on AHCI disks.

### Implementation
- Superblock, group descriptors, inodes
- Directory traversal
- Direct/indirect/double-indirect block reading
- VFS integration

---

## 🔄 PHASE 14: NETWORKING (virtio-net)

### Goal
Send/receive Ethernet frames via virtio-net.

### Implementation
- PCI virtio device discovery
- Virtqueue setup (rx, tx)
- ARP, IP, UDP protocol stacks
- Socket API

---

## 🔄 PHASE 15: FRAMEBUFFER GRAPHICS

### Goal
Draw pixels and text on Limine's framebuffer.

### Implementation
- Pixel operations on linear framebuffer
- Built-in 8x16 bitmap font
- Character and string drawing
- Console integration

---

## 📋 EXECUTION CHECKLIST

For each phase, follow this exact sequence:

- [ ] Read all relevant existing files
- [ ] Create new header file(s)
- [ ] Create new source file(s)
- [ ] Update Makefile dependencies
- [ ] Integrate into `kernel/kernel.c`
- [ ] `make clean && make`
- [ ] If build fails → fix → retry
- [ ] `make run-hdd` (or appropriate test)
- [ ] Analyze serial output
- [ ] If boot fails → diagnose → fix → retry
- [ ] Document changes in NOTES.md
- [ ] Proceed to next phase

---

## 🐛 COMMON DEBUGGING TECHNIQUES

### Serial Output Analysis
The beacon trace `StGKI` pinpoints where execution stops:
- `S` = start.S reached
- `t` = stack set up
- `G` = GDT loaded and CS reloaded
- `K` = kernel_main entered
- `I` = serial_init completed

### QEMU Debugging
```bash
# With CPU logging
qemu-system-x86_64 -M q35 -m 2G -hda build/archforge.img \
    -serial stdio -display none \
    -d int,cpu_reset,guest_errors \
    -D /tmp/qemu.log

# With GDB
qemu-system-x86_64 -M q35 -m 2G -hda build/archforge.img \
    -serial stdio -s -S
# In another terminal: gdb build/kernel.elf
# (gdb) target remote localhost:1234
# (gdb) break kernel_main
# (gdb) continue
```

### Kernel Panic Analysis
When an exception handler fires, the serial output shows:
- Vector number and name
- Error code (for some exceptions)
- RIP (faulting instruction)
- CR2 (for page faults — the faulting address)
- Register dump

Use RIP with `objdump -d build/kernel.elf` to find the faulting instruction.

---

## 🔒 SAFETY RULES

1. **NEVER disable interrupts (`cli`) in normal code paths** — only in critical sections.
2. **ALWAYS validate pointers** before dereferencing in interrupt handlers.
3. **ALREADY use `volatile`** for memory-mapped I/O and shared state.
4. **NEVER assume physical addresses** — always use HHDM offset.
5. **ALWAYS send EOI** to the PIC after handling an IRQ.
6. **ALWAYS invalidate TLB** after modifying page tables.
7. **ALWAYS check allocation returns** for NULL before use.

---

## 📚 REFERENCE MATERIALS

### Limine Protocol
- Official header: `include/limine.h`
- Spec: https://github.com/limine-bootloader/limine/blob/trunk/PROTOCOL.md

### x86_64 Architecture
- Intel SDM Volume 3: System Programming Guide
- OSDev Wiki: https://wiki.osdev.org

### Useful Tools
- `readelf`, `objdump`, `nm` — ELF inspection
- `xorriso` — ISO manipulation
- `qemu-system-x86_64` — emulation
- `gdb` — debugging

---

## 🎯 START HERE

1. Run **PHASE 0** to verify current state
2. If everything works → proceed to **PHASE 1 (VMM)**
3. Work through phases sequentially
4. After each phase → update NOTES.md
5. If stuck → use debugging techniques above
6. Report findings with exact serial output

**You have full autonomy. Make it work.**
