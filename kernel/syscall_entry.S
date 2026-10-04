/* kernel/syscall_entry.S — Assembly syscall entry point
 * 
 * Entry point for syscall/sysret instructions.
 * Called when user-space executes `syscall` instruction.
 * 
 * x86_64 syscall convention:
 *   RAX = syscall number
 *   RDI, RSI, RDX, R10, R8, R9 = arguments 1-6
 *   RCX = return RIP (saved by CPU)
 *   R11 = RFLAGS (saved by CPU)
 * 
 * On return:
 *   RAX = return value
 *   RCX, R11 restored by sysret
 * 
 * We save all registers, call C handler, then restore.
 */
.section .text
.global syscall_entry
.type syscall_entry, @function

syscall_entry:
    /* We're already on kernel stack (IST or kernel stack) */
    /* Syscall instruction saves: RCX=RIP, R11=RFLAGS */
    /* We need to save all registers to process context */
    
    /* Save user registers that syscall clobbers */
    push %rcx          /* Save user RIP */
    push %r11          /* Save user RFLAGS */
    
    /* Save all general-purpose registers */
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %rsp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    
    /* RAX now contains syscall number (from user) */
    /* Arguments in RDI, RSI, RDX, R10, R8, R9 (already in place) */
    
    /* Call C handler */
    mov %rsp, %rdi      /* First arg: pointer to saved registers */
    call syscall_handler
    
    /* Return value in RAX */
    /* Restore registers */
    pop %r15
    pop %r14
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rsp
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    
    /* Restore RFLAGS and RIP for sysret */
    pop %r11
    pop %rcx
    
    /* sysret will:
     *   RIP = RCX
     *   RFLAGS = R11
     *   RSP = user RSP (must be set in IA32_SYSCALL_RSP MSR or we use stack)
     *   CS = IA32_STAR[63:48] + 16 (user CS)
     *   SS = IA32_STAR[63:48] + 8  (user SS)
     */
    swapgs              /* Switch back to user GS */
    sysretq