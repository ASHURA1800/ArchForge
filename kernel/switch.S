/* kernel/switch.S — Assembly context switch
 * 
 * Switches from one process context to another.
 * Saves all registers of old process, restores registers of new process.
 * Also switches page tables (CR3) if the new process has a different PML4.
 * 
 * Arguments (System V AMD64):
 *   RDI = old process (process_t *)
 *   RSI = new process (process_t *)
 */
.section .text
.global switch_context
.type switch_context, @function

switch_context:
    /* Check if we need to switch page tables */
    test %rdi, %rdi
    jz load_new_pml4
    
    test %rsi, %rsi
    jz 1f
    
    mov 0x10(%rdi), %rax   /* old->pml4 (physical address) */
    mov 0x10(%rsi), %rdx   /* new->pml4 (physical address) */
    cmp %rax, %rdx
    je 1f                  /* Same PML4, skip CR3 switch */

load_new_pml4:
    /* Load new PML4 into CR3 */
    mov 0x10(%rsi), %rax
    test %rax, %rax
    jz 1f
    mov %rax, %cr3

1:
    /* Save old process context (if not NULL) */
    test %rdi, %rdi
    jz 2f  /* If old is NULL, skip saving */
    
    /* Save all registers to old process context */
    mov %rax, 0x18(%rdi)    /* context.rax */
    mov %rbx, 0x20(%rdi)    /* context.rbx */
    mov %rcx, 0x28(%rdi)    /* context.rcx */
    mov %rdx, 0x30(%rdi)    /* context.rdx */
    mov %rsi, 0x38(%rdi)    /* context.rsi */
    
    /* Save RDI (we're overwriting it, save to a temp or push) */
    push %rdi
    mov %rdi, 0x40(%rdi)    /* context.rdi (careful, self-modifying, but works if we read before write) */
    /* Actually, better to save RDI last or use a different register */
    pop %rdi
    mov %rdi, 0x40(%rdi)    /* context.rdi */
    
    mov %rbp, 0x48(%rdi)    /* context.rbp */
    mov %rsp, 0x50(%rdi)    /* context.rsp */
    
    /* Save R8-R15 */
    mov %r8, 0x58(%rdi)
    mov %r9, 0x60(%rdi)
    mov %r10, 0x68(%rdi)
    mov %r11, 0x70(%rdi)
    mov %r12, 0x78(%rdi)
    mov %r13, 0x80(%rdi)
    mov %r14, 0x88(%rdi)
    mov %r15, 0x90(%rdi)
    
    /* Save segment registers */
    mov %cs, 0x98(%rdi)     /* context.cs */
    mov %ss, 0xA0(%rdi)     /* context.ss */
    mov %ds, 0xA8(%rdi)     /* context.ds */
    mov %es, 0xB0(%rdi)     /* context.es */
    mov %fs, 0xB8(%rdi)     /* context.fs */
    mov %gs, 0xC0(%rdi)     /* context.gs */
    
    /* Save RFLAGS */
    pushfq
    pop %rax
    mov %rax, 0xA0(%rdi)    /* Wait, rflags is after gs? Let's check struct layout */
    /* 
     * struct context {
     *   rax(0), rbx(8), rcx(10), rdx(18), rsi(20), rdi(28), rbp(30), rsp(38),
     *   r8(40), r9(48), r10(50), r11(58), r12(60), r13(68), r14(70), r15(78),
     *   rip(80), rflags(88), cs(90), ss(98), ds(A0), es(A8), fs(B0), gs(B8)
     * }
     * Let's use explicit offsets based on the struct:
     * rax: 0x00, rbx: 0x08, rcx: 0x10, rdx: 0x18, rsi: 0x20, rdi: 0x28, rbp: 0x30, rsp: 0x38
     * r8: 0x40, r9: 0x48, r10: 0x50, r11: 0x58, r12: 0x60, r13: 0x68, r14: 0x70, r15: 0x78
     * rip: 0x80, rflags: 0x88, cs: 0x90, ss: 0x98, ds: 0xA0, es: 0xA8, fs: 0xB0, gs: 0xB8
     */
    
2:
    test %rdi, %rdi
    jz 3f
    
    mov %rax, 0x00(%rdi)
    mov %rbx, 0x08(%rdi)
    mov %rcx, 0x10(%rdi)
    mov %rdx, 0x18(%rdi)
    mov %rsi, 0x20(%rdi)
    
    /* We need to save RDI. We can push it, but we already pushed/popped. 
       Let's just save it to a known safe place or use RAX temporarily */
    mov %rdi, %r11
    mov %r11, 0x28(%rdi)
    
    mov %rbp, 0x30(%rdi)
    mov %rsp, 0x38(%rdi)
    
    mov %r8, 0x40(%rdi)
    mov %r9, 0x48(%rdi)
    mov %r10, 0x50(%rdi)
    /* r11 is used, skip or save it */
    mov %r12, 0x60(%rdi)
    mov %r13, 0x68(%rdi)
    mov %r14, 0x70(%rdi)
    mov %r15, 0x78(%rdi)
    
    mov %cs, 0x90(%rdi)
    mov %ss, 0x98(%rdi)
    mov %ds, 0xA0(%rdi)
    mov %es, 0xA8(%rdi)
    mov %fs, 0xB0(%rdi)
    mov %gs, 0xB8(%rdi)
    
    pushfq
    pop %rax
    mov %rax, 0x88(%rdi)
    
    mov 0(%rsp), %rax
    mov %rax, 0x80(%rdi)

3:
    /* Restore new process context */
    test %rsi, %rsi
    jz 4f
    
    /* Restore segment registers first */
    mov 0xB8(%rsi), %rax
    mov %rax, %gs
    mov 0xB0(%rsi), %rax
    mov %rax, %fs
    mov 0xA8(%rsi), %rax
    mov %rax, %es
    mov 0xA0(%rsi), %rax
    mov %rax, %ds
    
    /* Restore R8-R15 */
    mov 0x78(%rsi), %r15
    mov 0x70(%rsi), %r14
    mov 0x68(%rsi), %r13
    mov 0x60(%rsi), %r12
    mov 0x58(%rsi), %r11
    mov 0x50(%rsi), %r10
    mov 0x48(%rsi), %r9
    mov 0x40(%rsi), %r8
    
    /* Restore RFLAGS */
    mov 0x88(%rsi), %rax
    push %rax
    popfq
    
    /* Build the far return frame on the NEW stack for lretq */
    mov 0x38(%rsi), %rsp   /* Switch to new stack */
    
    mov 0x98(%rsi), %rax   /* SS */
    push %rax
    mov 0x38(%rsi), %rax   /* RSP */
    push %rax
    mov 0x90(%rsi), %rax   /* CS */
    push %rax
    mov 0x80(%rsi), %rax   /* RIP */
    push %rax
    
    /* Restore other general-purpose registers */
    mov 0x30(%rsi), %rbp
    mov 0x28(%rsi), %rdi
    mov 0x20(%rsi), %rsi
    mov 0x18(%rsi), %rdx
    mov 0x10(%rsi), %rcx
    mov 0x08(%rsi), %rbx
    mov 0x00(%rsi), %rax
    
    /* Far return - pops RIP, CS, (and RSP, SS if privilege level changes) */
    lretq
    
4:
    ret