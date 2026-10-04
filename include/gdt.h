#ifndef ARCHFORGE_GDT_H
#define ARCHFORGE_GDT_H

#include <stdint.h>

// GDT Descriptor Structure (16 bytes per entry for TSS, 8 bytes for regular)
// Packed to ensure no compiler padding interferes with CPU expectations
struct gdt_descriptor {
    uint16_t limit_low;      // Bits 0-15 of segment limit
    uint16_t base_low;       // Bits 0-15 of segment base
    uint8_t  base_mid;       // Bits 16-23 of segment base
    uint8_t  access;         // Access flags (Present, DPL, Type, etc.)
    uint8_t  granularity;    // Flags (Limit 16-19, 64-bit flag, etc.)
    uint8_t  base_high;      // Bits 24-31 of segment base
} __attribute__((packed));

// TSS Structure (x86_64 requires 104 bytes minimum)
// This structure defines the hardware state saved during task switches or IST usage
struct tss_entry {
    uint32_t reserved0;      // Reserved, must be 0
    uint64_t rsp0;           // Stack pointer for ring 0 (used on privilege level changes)
    uint64_t rsp1;           // Stack pointer for ring 1 (unused in typical kernels)
    uint64_t rsp2;           // Stack pointer for ring 2 (unused in typical kernels)
    uint64_t ist1;           // Interrupt Stack Table 1 (e.g., for Double Fault)
    uint64_t ist2;           // IST 2 (e.g., for NMI)
    uint64_t ist3;           // IST 3 (e.g., for Page Fault - CRITICAL)
    uint64_t ist4;           // IST 4
    uint64_t ist5;           // IST 5
    uint64_t ist6;           // IST 6
    uint64_t ist7;           // IST 7
    uint64_t reserved1;      // Reserved
    uint16_t reserved2;      // Reserved
    uint16_t iomap_base;     // I/O Map Base Address (0xFFFF means no I/O map)
} __attribute__((packed));

// GDT Pointer structure for the lgdt instruction
// The CPU expects a 16-bit limit followed by a 64-bit base address
struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// Function prototypes
void gdt_init(void);
void tss_set_rsp0(uint64_t rsp);
void tss_set_ist3(uint64_t rsp);

#endif // ARCHFORGE_GDT_H