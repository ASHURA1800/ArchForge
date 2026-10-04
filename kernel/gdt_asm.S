.global gdt_load
.type gdt_load, @function
gdt_load:
    // %rdi contains pointer to gdt_ptr
    lgdt (%rdi)

    // Reload data segment registers to ensure they use the new GDT
    // Selector 0x10 = Index 2 (Kernel Data) * 8 + RPL 0
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    // Far jump to reload CS (Code Segment)
    // This is required because the CPU caches the CS descriptor, and we must force it to fetch the new one
    pushq $0x08          // Kernel Code Segment selector (Index 1 * 8 = 0x08)
    leaq .Lreload_cs(%rip), %rax
    pushq %rax
    lretq                // Long return, pops RIP then CS

.Lreload_cs:
    // After lretq, execution continues here with the new CS
    ret

.global tss_load
.type tss_load, @function
tss_load:
    // %di contains the TSS selector (0x28)
    // ltr loads the Task Register, marking the TSS as "busy" in the GDT
    ltr %di
    ret