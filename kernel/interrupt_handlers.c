/* kernel/interrupt_handlers.c — C handlers for ISRs and IRQs
 *
 * These are called from the assembly stubs in isrs.S after all
 * registers have been saved to the stack.
 */
#include "idt.h"
#include "serial.h"
#include "io.h"
#include "keyboard.h"
#include "vmm.h"

/* Exception names for pretty-printing */
static const char *exception_names[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
};


/* ====================================================================
 * isr_handler — Called for CPU exceptions (vectors 0-31)
 * ==================================================================== */
void isr_handler(struct interrupt_frame *frame) {
    uint64_t int_num = frame->int_number;

    serial_write("\n");
    serial_write("========================================\n");
    serial_write("  EXCEPTION CAUGHT\n");
    serial_write("========================================\n");

    serial_write("Vector:    ");
    serial_write_dec(int_num);
    serial_write(" (0x");
    serial_write_hex(int_num);
    serial_write(")\n");

    if (int_num < 22) {
        serial_write("Name:      ");
        serial_write(exception_names[int_num]);
        serial_write("\n");
    }

    serial_write("Error code: ");
    serial_write_hex(frame->error_code);
    serial_write("\n");

    serial_write("RIP:       ");
    serial_write_hex(frame->rip);
    serial_write("\n");
    serial_write("CS:        ");
    serial_write_hex(frame->cs);
    serial_write("\n");
    serial_write("RFLAGS:    ");
    serial_write_hex(frame->rflags);
    serial_write("\n");

    /* For Page Fault (#14), handle it via VMM */
    if (int_num == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        
        /* Call the VMM page fault handler */
        handle_page_fault(frame->error_code, cr2);
        
        /* If we return from handle_page_fault, it was resolved (e.g., COW).
         * We need to restore registers and return normally. */
        goto restore_and_return;
    }

restore_and_return:
    /* Register dump (only if not page fault) */
    if (int_num != 14) {
        serial_write("\nRegister dump:\n");
        serial_write("  RAX="); serial_write_hex(frame->rax);
        serial_write("  RBX="); serial_write_hex(frame->rbx);
        serial_write("  RCX="); serial_write_hex(frame->rcx);
        serial_write("\n  RDX="); serial_write_hex(frame->rdx);
        serial_write("  RSI="); serial_write_hex(frame->rsi);
        serial_write("  RDI="); serial_write_hex(frame->rdi);
        serial_write("\n  RBP="); serial_write_hex(frame->rbp);
        serial_write("  RSP="); serial_write_hex(frame->rsp);
        serial_write("\n");

        serial_write("\nSystem halted.\n");

        /* Halt forever */
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }
}


/* ====================================================================
 * irq_handler — Called for hardware interrupts (vectors 32+)
 * ==================================================================== */
void irq_handler(struct interrupt_frame *frame) {
    uint64_t irq = frame->int_number - 32; /* Convert vector to IRQ number */

    if (irq == 0) {
        /* Timer (IRQ0) — just send EOI */
        outb(0x20, 0x20);  /* Send EOI to master PIC */

    } else if (irq == 1) {
        /* Keyboard (IRQ1) */
        keyboard_irq_handler();

    } else {
        /* Unknown IRQ — send EOI anyway */
        if (irq >= 8) {
            outb(0xA0, 0x20);  /* Send EOI to slave PIC */
        }
        outb(0x20, 0x20);  /* Send EOI to master PIC */
    }
}
