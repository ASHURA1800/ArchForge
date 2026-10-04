#include "idt.h"
#include "serial.h"
#include "io.h"

static struct idt_entry idt[256];
static struct idt_ptr idt_ptr;

extern void idt_load(struct idt_ptr *ptr);

// PIC Remapping (8259A)
// The PIC defaults to IRQ 0-7 mapping to vectors 0x08-0x0F, which conflicts with CPU exceptions (0x00-0x1F).
// We must remap them to 0x20-0x2F to avoid this conflict.
static void pic_remap(void) {
    serial_write("[IDT] Remapping PIC...\n");

    // ICW1: Initialization command, ICW4 needed, edge-triggered
    outb(0x20, 0x11);
    outb(0xA0, 0x11);

    // ICW2: Offset vectors. IRQ 0-7 -> 0x20-0x27, IRQ 8-15 -> 0x28-0x2F
    outb(0x21, 0x20);
    outb(0xA1, 0x28);

    // ICW3: Tell master there is a slave at IRQ2 (0x04), tell slave its cascade identity (0x02)
    outb(0x21, 0x04);
    outb(0xA1, 0x02);

    // ICW4: 8086 mode, normal EOI (End of Interrupt)
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    // Mask all interrupts initially
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    serial_write("[IDT] PIC remapped to 0x20-0x2F.\n");
}

void idt_init(void) {
    serial_write("[IDT] Initializing Interrupt Descriptor Table...\n");
    pic_remap();

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;

    // Set up critical gates
    // Type 0x8E = 1 (Present) | 00 (Ring 0) | 0 (Reserved) | 1110 (64-bit Interrupt Gate)
    idt_set_gate(0, (uint64_t)isr0, 0x08, 0, 0x8E);    // Divide by zero (No error code)
    idt_set_gate(14, (uint64_t)isr14, 0x08, 3, 0x8E);  // Page Fault (Has error code, uses IST3)
    idt_set_gate(32, (uint64_t)irq32, 0x08, 0, 0x8E);   // Timer (IRQ0)
    idt_set_gate(33, (uint64_t)irq33, 0x08, 0, 0x8E);   // Keyboard (IRQ1)

    idt_load(&idt_ptr);
    serial_write("[IDT] IDT loaded successfully.\n");

    // Enable timer interrupt (IRQ0) and keyboard (IRQ1) on master PIC
    uint8_t master_mask = inb(0x21);
    master_mask &= ~0x03; // Unmask IRQ0 and IRQ1
    outb(0x21, master_mask);
    serial_write("[IDT] Timer and Keyboard interrupts enabled.\n");
}

void idt_set_gate(uint8_t num, uint64_t base, uint16_t selector, uint8_t ist, uint8_t type_attr) {
    idt[num].offset_low = (uint16_t)(base & 0xFFFF);
    idt[num].selector = selector;
    idt[num].ist = ist & 0x07; // Lower 3 bits for IST
    idt[num].type_attr = type_attr;
    idt[num].offset_mid = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt[num].zero = 0;
}