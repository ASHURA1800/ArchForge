/* kernel/syscall.c — System Call Implementation
 * 
 * Handles all system calls from user-space via syscall/sysret.
 */
#include "syscall.h"
#include "process.h"
#include "heap.h"
#include "serial.h"
#include "vmm.h"
#include "pmm.h"
#include "ramfs.h"
#include "keyboard.h"
#include "timer.h"
#include "msr.h"
#include "elf.h"
#include "vfs.h"
#include "../include/stddef.h"
#include "../include/stdint.h"
#include "../include/string.h"

/* Syscall table */
static void *syscall_table[MAX_SYSCALLS] = {
    [SYS_EXIT]      = (void *)sys_exit,
    [SYS_WRITE]     = (void *)sys_write,
    [SYS_READ]      = (void *)sys_read,
    [SYS_YIELD]     = (void *)sys_yield,
    [SYS_GETPID]    = (void *)sys_getpid,
    [SYS_SLEEP_MS]  = (void *)sys_sleep_ms,
    [SYS_MMAP]      = (void *)sys_mmap,
    [SYS_MUNMAP]    = (void *)sys_munmap,
    [SYS_FORK]      = (void *)sys_fork,
    [SYS_EXEC]      = (void *)sys_exec,
    [SYS_WAITPID]   = (void *)sys_waitpid,
    [SYS_OPEN]      = (void *)sys_open,
    [SYS_CLOSE]     = (void *)sys_close,
    [SYS_STAT]      = (void *)sys_stat,
    [SYS_LSEEK]     = (void *)sys_lseek,
    [SYS_IOCTL]     = (void *)sys_ioctl,
};

/* MSR addresses for syscall/sysret */
#define MSR_IA32_STAR          0xC0000081
#define MSR_IA32_LSTAR         0xC0000082
#define MSR_IA32_FMASK         0xC0000084
#define MSR_IA32_SYSCALL_RSP   0xC0000085

/* External declarations */
extern void syscall_entry(void);
extern process_t *current_process;
extern process_t *ready_list;
extern uint64_t next_pid;

/* ====================================================================
 * syscall_init — Initialize syscall interface
 * ==================================================================== */
void syscall_init(void) {
    serial_write("[SYSCALL] Initializing system call interface...\n");
    
    /* Set up MSRs for syscall/sysret */
    /* STAR: legacy syscall (unused), but sets CS/SS for sysret */
    wrmsr(MSR_IA32_STAR, 
        ((uint64_t)0x23 << 48) |   /* User SS (0x23 = GDT index 4, RPL=3) */
        ((uint64_t)0x1B << 32));   /* User CS (0x1B = GDT index 3, RPL=3) */
    
    /* LSTAR: 64-bit syscall entry point */
    extern void syscall_entry(void);
    wrmsr(MSR_IA32_LSTAR, (uint64_t)syscall_entry);
    
    /* FMASK: RFLAGS bits to clear on syscall (IF=1<<9, TF=1<<8) */
    wrmsr(MSR_IA32_FMASK, 0x200 | 0x100);
    
    serial_write("[SYSCALL] System call interface initialized.\n");
}

/* ====================================================================
 * syscall_handler — C handler for syscalls
 * ==================================================================== */
void syscall_handler(void *saved_regs) {
    uint64_t *regs = (uint64_t *)saved_regs;
    
    uint64_t syscall_num = regs[0];
    
    if (syscall_num >= MAX_SYSCALLS || syscall_table[syscall_num] == NULL) {
        regs[0] = -1;  /* Return -ENOSYS */
        return;
    }
    
    /* Extract arguments */
    uint64_t arg1 = regs[5];  /* RDI */
    uint64_t arg2 = regs[4];  /* RSI */
    uint64_t arg3 = regs[3];  /* RDX */
    uint64_t arg4 = regs[10]; /* R10 */
    uint64_t arg5 = regs[8];  /* R8 */
    uint64_t arg6 = regs[9];  /* R9 */
    
    /* Call syscall function */
    int64_t (*fn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) 
        = (int64_t(*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t))
        syscall_table[regs[0]];
    
    int64_t result = fn(arg1, arg2, arg3, arg4, arg5, arg6);
    
    /* Store return value in RAX */
    regs[0] = (uint64_t)result;
}

/* ====================================================================
 * Syscall Implementations
 * ==================================================================== */

int64_t sys_exit(int code) {
    process_exit(code);
    return 0;
}

int64_t sys_write(int fd, const void *buf, size_t count) {
    if (!buf || count == 0) return -1;
    process_t *proc = process_current();
    if (!proc) return -1;
    
    if (fd == 1 || fd == 2) {
        /* Serial console - write actual characters, not hex */
        for (size_t i = 0; i < count; i++) {
            char c = ((const char *)buf)[i];
            if (c == '\n') {
                serial_write("\r\n");
            } else {
                char str[2] = {c, '\0'};
                serial_write(str);
            }
        }
        return count;
    }
    
    if (fd >= 3 && fd < MAX_FDS && proc->fd_table[fd].in_use) {
        vfs_node_t *node = (vfs_node_t *)proc->fd_table[fd].node;
        if (node && node->type == VFS_TYPE_FILE) {
            int res = vfs_write(node, buf, count, proc->fd_table[fd].offset);
            if (res > 0) {
                proc->fd_table[fd].offset += res;
            }
            return res;
        }
    }
    return -1; /* EBADF */
}

int64_t sys_read(int fd, void *buf, size_t count) {
    if (!buf || count == 0) return -1;
    process_t *proc = process_current();
    if (!proc) return -1;
    
    if (fd == 0) {
        /* Keyboard */
        char c = keyboard_getc_nb();
        if (c) {
            ((char *)buf)[0] = c;
            return 1;
        }
        return 0;
    }
    
    if (fd >= 3 && fd < MAX_FDS && proc->fd_table[fd].in_use) {
        vfs_node_t *node = (vfs_node_t *)proc->fd_table[fd].node;
        if (node && node->type == VFS_TYPE_FILE) {
            int res = vfs_read(node, buf, count, proc->fd_table[fd].offset);
            if (res > 0) {
                proc->fd_table[fd].offset += res;
            }
            return res;
        }
    }
    return -1; /* EBADF */
}

int64_t sys_yield(void) {
    process_yield();
    return 0;
}

int64_t sys_getpid(void) {
    process_t *proc = process_current();
    return proc ? proc->pid : 0;
}

int64_t sys_sleep_ms(uint64_t ms) {
    timer_sleep_ms(ms);
    return 0;
}

int64_t sys_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset) {
    (void)prot; (void)flags; (void)fd; (void)offset;
    size_t pages = (len + 4095) / 4096;
    void *phys = pmm_alloc(pages);
    if (!phys) return -1;
    
    uint64_t virt = (uint64_t)addr;
    if (addr == NULL) {
        virt = 0x100000000000;
    }
    
    for (size_t i = 0; i < pages; i++) {
        if (vmm_map_page(virt + i * 4096, (uint64_t)phys + i * 4096, 
                        PTE_PRESENT | PTE_WRITABLE | PTE_USER) != 0) {
            return -1;
        }
    }
    return (int64_t)virt;
}

int64_t sys_munmap(void *addr, size_t len) {
    size_t pages = (len + 4095) / 4096;
    uint64_t virt = (uint64_t)addr;
    
    for (size_t i = 0; i < pages; i++) {
        vmm_unmap_page(virt + i * 4096);
    }
    return 0;
}

int64_t sys_fork(void) {
    process_t *parent = process_current();
    if (!parent) return -1;
    
    process_t *child = kmalloc(sizeof(process_t));
    if (!child) return -1;
    
    *child = *parent;
    child->pid = next_pid++;
    child->state = PROCESS_STATE_READY;
    child->parent = parent;
    child->exit_code = 0;
    child->total_ticks = 0;
    child->time_slice = PROCESS_TIME_SLICE;
    
    child->stack = kmalloc(parent->stack_size);
    if (!child->stack) {
        kfree(child);
        return -1;
    }
    memcpy(child->stack, parent->stack, parent->stack_size);
    
    child->context.rax = 0;
    
    if (ready_list) {
        process_t *last = ready_list;
        while (last->next) last = last->next;
        last->next = child;
    } else {
        ready_list = child;
    }
    child->next = NULL;
    
    return child->pid;
}

int64_t sys_exec(const char *path, char *const argv[], char *const envp[]) {
    if (!path) return -1;  /* EINVAL */
    
    process_t *proc = process_current();
    if (!proc) return -1;  /* No current process */
    
    serial_write("[SYSCALL] exec: ");
    serial_write(path);
    serial_write("\n");
    
    /* Read file from VFS */
    vfs_node_t *node = vfs_resolve(path);
    if (!node || node->type != VFS_TYPE_FILE) {
        serial_write("[SYSCALL] exec: file not found\n");
        return -2;  /* ENOENT */
    }
    
    size_t file_size = node->size;
    uint8_t *file_data = kmalloc(file_size);
    if (!file_data) {
        serial_write("[SYSCALL] exec: out of memory\n");
        return -12;  /* ENOMEM */
    }
    
    if (vfs_read(node, file_data, file_size, 0) < 0) {
        kfree(file_data);
        return -5;  /* EIO */
    }
    
    /* Load ELF */
    elf_info_t elf_info;
    if (elf_load(file_data, file_size, &elf_info) != 0) {
        kfree(file_data);
        serial_write("[SYSCALL] exec: invalid ELF\n");
        return -8;  /* ENOEXEC */
    }
    
    kfree(file_data);
    
    /* Save old stack pointer and context */
    void *old_stack = proc->stack;
    size_t old_stack_size = proc->stack_size;
    
    /* Update process context for new program */
    proc->context.rip = elf_info.entry_point;
    proc->context.rsp = elf_info.stack_top;
    proc->context.rflags = 0x202;  /* IF=1 */
    
    /* Ensure user mode segments */
    proc->context.cs = 0x1B;  /* User code segment */
    proc->context.ss = 0x23;  /* User data segment */
    proc->context.ds = 0x23;
    proc->context.es = 0x23;
    proc->context.fs = 0x23;
    proc->context.gs = 0x23;
    
    /* Clear other registers */
    proc->context.rax = 0;
    proc->context.rbx = 0;
    proc->context.rcx = 0;
    proc->context.rdx = 0;
    proc->context.rsi = 0;
    proc->context.rdi = 0;
    proc->context.rbp = 0;
    proc->context.r8 = 0;
    proc->context.r9 = 0;
    proc->context.r10 = 0;
    proc->context.r11 = 0;
    proc->context.r12 = 0;
    proc->context.r13 = 0;
    proc->context.r14 = 0;
    proc->context.r15 = 0;
    
    /* Free old stack */
    if (old_stack) {
        kfree(old_stack);
    }
    
    proc->stack = NULL;  /* Stack is managed by ELF loader now */
    proc->stack_size = 0;
    
    /* TODO: Set up argv/envp on user stack */
    
    serial_write("[SYSCALL] exec: successfully loaded ");
    serial_write(path);
    serial_write("\n");
    
    return 0;  /* Never returns to caller, sysret will go to new program */
}

int64_t sys_waitpid(pid_t pid, int *status, int options) {
    (void)pid; (void)status; (void)options;
    /* TODO: Implement proper waitpid with process state tracking */
    return -1;
}

int64_t sys_open(const char *path, int flags, ...) {
    if (!path) return -1;
    process_t *proc = process_current();
    if (!proc) return -1;
    
    vfs_node_t *node = vfs_resolve(path);
    if (!node) return -2; /* ENOENT */
    
    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++) {
        if (!proc->fd_table[i].in_use) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -24; /* EMFILE */
    
    proc->fd_table[fd].in_use = 1;
    proc->fd_table[fd].node = node;
    proc->fd_table[fd].offset = 0;
    proc->fd_table[fd].flags = flags;
    
    return fd;
}

int64_t sys_close(int fd) {
    process_t *proc = process_current();
    if (!proc) return -1;
    
    if (fd >= 3 && fd < MAX_FDS && proc->fd_table[fd].in_use) {
        proc->fd_table[fd].in_use = 0;
        proc->fd_table[fd].node = NULL;
        proc->fd_table[fd].offset = 0;
        return 0;
    }
    return -1; /* EBADF */
}