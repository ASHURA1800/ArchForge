/* kernel/start.S — ArchForge OS entry point
 * Limine hands us in 64-bit long mode with paging enabled.
 * We set up a stack, load our own GDT, and call kernel_main.
 *
 * Early serial beacons (raw outb, no stack, no calls) prove we reached
 * each stage even if everything else is broken.
 */

.section .text
.code64

.global _start
_start:
    /* ================================================================
     * BEACON 1: Print 'S' to COM1 (0x3F8) as the very first instruction.
     * No stack, no calls, no GDT reload — raw port I/O.
     * If you see 'S' in QEMU serial output, _start is being executed.
     * ================================================================ */
    movw    $0x3F8, %dx
    movb    $'S', %al
    outb    %al, %dx

    /* ================================================================
     * Set up our own stack BEFORE any call instruction.
     * Limine provides a valid GDT — we keep it and only set up our stack.
     * The full GDT with TSS will be loaded by gdt_init() in kernel_main.
     * ================================================================ */
    leaq    stack_top(%rip), %rsp

    /* Clear direction flag (required by C ABI) */
    cld

    /* BEACON 2: Print 't' — stack is set up */
    movw    $0x3F8, %dx
    movb    $'t', %al
    outb    %al, %dx

    /* ================================================================
     * BEACON 3: Print 'G' — ready to call kernel_main
     * Using Limine's GDT (already loaded and valid)
     * ================================================================ */
    movw    $0x3F8, %dx
    movb    $'G', %al
    outb    %al, %dx
    movb    $'\n', %al
    outb    %al, %dx

    /* ================================================================
     * Call kernel_main. At this point:
     * - We are in 64-bit long mode
     * - Paging is enabled (by Limine)
     * - Limine's GDT is loaded (we keep it for now)
     * - Stack is set up and 16-byte aligned
     * ================================================================ */
    call    kernel_main

    /* If kernel_main returns, halt forever */
.Lhalt:
    cli
    hlt
    jmp     .Lhalt


/* ====================================================================
 * Stack — 64 KiB, 16-byte aligned
 * ==================================================================== */
.section .bss
.align 16
stack_bottom:
    .skip   65536                   /* 64 KiB stack                    */
stack_top:
