#include "gdt.h"
#include "serial.h"
#include <stddef.h>

// Define the GDT array (7 entries: Null, Kernel Code, Kernel Data, User Code, User Data, TSS Low, TSS High)
// TSS descriptor in 64-bit mode takes TWO 8-byte slots (16 bytes total)
static struct gdt_descriptor gdt[7];
static struct gdt_ptr gdt_ptr;

// Define the TSS (placed in .bss, will be initialized)
static struct tss_entry tss __attribute__((aligned(16)));

// External assembly function to load GDT
extern void gdt_load(struct gdt_ptr *ptr);
// External assembly function to load TR register (Task Register)
extern void tss_load(uint16_t selector);

// Helper to set a standard GDT entry
static void gdt_set_gate(int num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_mid    = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access      = access;
}

void gdt_init(void) {
    serial_write("[GDT] Initializing Global Descriptor Table...\n");

    // 1. Null Descriptor (Required by x86 architecture)
    gdt_set_gate(0, 0, 0, 0, 0);

    // 2. Kernel Code Segment (Ring 0, Executable, Readable, 64-bit)
    // Access: 1(Present) 00(Ring 0) 1(Code) 1(Readable) 0(Accessed=0) -> 0x9A
    // Granularity: 1(64-bit) 0(Reserved) 0(Reserved) 0(Reserved) 0000(Limit) -> 0x20
    gdt_set_gate(1, 0, 0, 0x9A, 0x20);

    // 3. Kernel Data Segment (Ring 0, Writable)
    // Access: 1(Present) 00(Ring 0) 0(Data) 1(Writable) 0(Accessed=0) -> 0x92
    // Granularity: 0(Not 64-bit, data segments ignore this in 64-bit mode) -> 0x00
    gdt_set_gate(2, 0, 0, 0x92, 0x00);

    // 4. User Code Segment (Ring 3, Executable, Readable, 64-bit)
    // Access: 1(Present) 11(Ring 3) 1(Code) 1(Readable) 0(Accessed=0) -> 0xFA
    gdt_set_gate(3, 0, 0, 0xFA, 0x20);

    // 5. User Data Segment (Ring 3, Writable)
    // Access: 1(Present) 11(Ring 3) 0(Data) 1(Writable) 0(Accessed=0) -> 0xF2
    gdt_set_gate(4, 0, 0, 0xF2, 0x00);

    // 6. TSS Descriptor (64-bit TSS takes 16 bytes = 2 GDT entries)
        // Index 5 = low part, Index 6 = high part
        uint64_t tss_base = (uint64_t)&tss;
        uint32_t tss_limit = sizeof(struct tss_entry) - 1;

        // Lower 64 bits of the TSS descriptor (index 5)
        gdt[5].limit_low = tss_limit & 0xFFFF;
        gdt[5].base_low  = tss_base & 0xFFFF;
        gdt[5].base_mid  = (tss_base >> 16) & 0xFF;
        gdt[5].access    = 0x89; // Present (1), Ring 0 (00), 64-bit TSS type (1001 = 0x9)
        gdt[5].granularity = ((tss_limit >> 16) & 0x0F); // No 4KB granularity for TSS
        gdt[5].base_high = (tss_base >> 24) & 0xFF;

        // Upper 64 bits of the TSS descriptor (index 6) - contains base bits 32-63
        uint64_t *tss_desc_high = (uint64_t *)&gdt[6];
        *tss_desc_high = (tss_base >> 32) & 0xFFFFFFFF;

        // Setup GDT Pointer
        gdt_ptr.limit = sizeof(gdt) - 1;
        gdt_ptr.base  = (uint64_t)&gdt;

    // Load GDT using assembly
    gdt_load(&gdt_ptr);
    serial_write("[GDT] GDT loaded successfully.\n");

    // Initialize TSS (Zero out first to ensure no garbage values)
    for (size_t i = 0; i < sizeof(struct tss_entry); i++) {
        ((uint8_t *)&tss)[i] = 0;
    }

    // Set I/O Map Base to 0xFFFF to indicate no I/O permission bitmap
    tss.iomap_base = 0xFFFF;

    // Load the TSS selector (Index 5 * 8 bytes per entry = 0x28)
    tss_load(0x28);
    serial_write("[GDT] TSS loaded successfully.\n");
}

void tss_set_rsp0(uint64_t rsp) {
    tss.rsp0 = rsp;
}

void tss_set_ist3(uint64_t rsp) {
    tss.ist3 = rsp;
}