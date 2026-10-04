.global isr0
.global isr14
.global irq32
.global irq33
.global idt_load

.type idt_load, @function
idt_load:
    lidt (%rdi)
    ret

// Macro for exceptions without an error code
.macro ISR_NOERR num
.global isr\num
.type isr\num, @function
isr\num:
    pushq $0              // Dummy error code
    pushq $\num           // Interrupt number
    jmp isr_common_stub
.endm

// Macro for exceptions with an error code
.macro ISR_ERR num
.global isr\num
.type isr\num, @function
isr\num:
    pushq $\num           // Interrupt number (error code is already on stack below this)
    jmp isr_common_stub
.endm

// Macro for hardware IRQs
.macro IRQ num
.global irq\num
.type irq\num, @function
irq\num:
    pushq $0              // Dummy error code
    pushq $\num           // Interrupt number
    jmp irq_common_stub
.endm

ISR_NOERR 0
ISR_ERR 14
IRQ 32
IRQ 33

isr_common_stub:
    // Save all general-purpose registers
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movw %ds, %ax
    pushq %rax

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    movq %rsp, %rdi
    call isr_handler

    popq %rax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax

    addq $16, %rsp
    iretq

irq_common_stub:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movw %ds, %ax
    pushq %rax
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    movq %rsp, %rdi
    call irq_handler

    popq %rax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax

    addq $16, %rsp
    iretq