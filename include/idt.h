#ifndef ARCHFORGE_IDT_H
#define ARCHFORGE_IDT_H

#include <stdint.h>

/* IDT Entry Structure (16 bytes) for x86_64 */
struct idt_entry {
    uint16_t offset_low;    /* Lower 16 bits of handler address     */
    uint16_t selector;      /* Kernel code segment selector (0x08)  */
    uint8_t  ist;           /* IST index (bits 0-2), reserved (3-7) */
    uint8_t  type_attr;     /* Type and attributes (e.g., 0x8E)     */
    uint16_t offset_mid;    /* Middle 16 bits of handler address    */
    uint32_t offset_high;   /* Upper 32 bits of handler address     */
    uint32_t zero;          /* Reserved, must be 0                  */
} __attribute__((packed));

/* IDT Pointer structure for the lidt instruction */
struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/*
 * Full interrupt frame as it appears on the stack AFTER our assembly stub
 * has saved all general-purpose registers and segment registers.
 *
 * Stack layout (top to bottom, low to high addresses):
 *   [DS]           <- pushed by stub (saved segment)
 *   [R15..RAX]     <- pushed by stub (15 GP registers)
 *   [int_number]   <- pushed by ISR/IRQ stub
 *   [error_code]   <- pushed by CPU (for some exceptions) or stub (dummy 0)
 *   [RIP]          <- pushed by CPU
 *   [CS]           <- pushed by CPU
 *   [RFLAGS]       <- pushed by CPU
 *   [RSP]          <- pushed by CPU (only on privilege change)
 *   [SS]           <- pushed by CPU (only on privilege change)
 */
struct interrupt_frame {
    /* Segment registers saved by our stub */
    uint64_t ds;

    /* General-purpose registers saved by our stub (in push order) */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;

    /* Pushed by our ISR/IRQ stub */
    uint64_t int_number;
    uint64_t error_code;

    /* Pushed by the CPU */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;   /* Only valid on privilege level change */
    uint64_t ss;    /* Only valid on privilege level change */
} __attribute__((packed));

void idt_init(void);
void idt_set_gate(uint8_t num, uint64_t base, uint16_t selector, uint8_t ist, uint8_t type_attr);

/* External assembly ISR stubs */
extern void isr0(void);   /* Divide by zero          */
extern void isr14(void);  /* Page fault              */
extern void irq32(void);  /* Timer (IRQ0)            */
extern void irq33(void);  /* Keyboard (IRQ1)         */

#endif /* ARCHFORGE_IDT_H */
