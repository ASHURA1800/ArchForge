/* kernel/process.c — Process Management & Scheduler
 * 
 * Round-robin preemptive multitasking with per-process page tables and COW.
 */
#include "process.h"
#include "heap.h"
#include "serial.h"
#include "elf.h"
#include "vmm.h"
#include "pmm.h"
#include "../include/stddef.h"
#include "../include/string.h"

/* Maximum processes */
#define MAX_PROCESSES 64

/* Time slice for round-robin (ms) */
#define PROCESS_TIME_SLICE 10

/* Default stack size */
#define DEFAULT_STACK_SIZE (16 * 1024)  /* 16 KB */

/* Process table */
static process_t processes[MAX_PROCESSES];
process_t *current_process = NULL;
process_t *ready_list = NULL;
uint64_t next_pid = 1;

/* External: Limine's initial PML4 (kernel space) */
extern uint64_t *kernel_pml4;

/* Forward declarations */
static void process_switch_context(process_t *old, process_t *new);
static void process_start(void);

/* ====================================================================
 * process_init — Initialize process subsystem
 * ==================================================================== */
void process_init(void) {
    serial_write("[PROCESS] Initializing process subsystem...\n");
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].state = PROCESS_STATE_UNUSED;
        processes[i].pid = 0;
        processes[i].next = NULL;
        processes[i].stack = NULL;
        processes[i].pml4 = NULL;
    }
    
    current_process = NULL;
    ready_list = NULL;
    next_pid = 1;
    
    serial_write("[PROCESS] Process subsystem initialized.\n");
}

/* ====================================================================
 * process_create — Create a new kernel process
 * ==================================================================== */
process_t *process_create(void (*entry)(void), void *arg) {
    process_t *proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_UNUSED) {
            proc = &processes[i];
            break;
        }
    }
    
    if (!proc) {
        serial_write("[PROCESS] ERROR: Process table full!\n");
        return NULL;
    }
    
    /* Allocate new PML4 */
    proc->pml4 = (uint64_t *)pmm_alloc(1);
    if (!proc->pml4) {
        serial_write("[PROCESS] ERROR: Failed to allocate PML4!\n");
        return NULL;
    }
    
    /* Zero it */
    #define HHDM_OFFSET 0xffff800000000000ULL
    uint64_t *pml4_virt_ptr = (uint64_t *)((uint64_t)proc->pml4 + HHDM_OFFSET);
    for (int i = 0; i < 512; i++) {
        pml4_virt_ptr[i] = 0;
    }
    
    /* Clone kernel space from current (or initial) PML4 */
    extern uint64_t *initial_pml4;
    vmm_clone_user_space(initial_pml4, pml4_virt_ptr);
    
    proc->stack_size = DEFAULT_STACK_SIZE;
    proc->stack = kmalloc(proc->stack_size);
    if (!proc->stack) {
        pmm_free(proc->pml4, 1);
        return NULL;
    }
    
    proc->pid = next_pid++;
    proc->state = PROCESS_STATE_READY;
    proc->exit_code = 0;
    proc->total_ticks = 0;
    proc->time_slice = PROCESS_TIME_SLICE;
    proc->parent = current_process;
    proc->next = NULL;
    
    /* Initialize file descriptor table */
    for (int i = 0; i < MAX_FDS; i++) {
        proc->fd_table[i].in_use = 0;
        proc->fd_table[i].node = NULL;
        proc->fd_table[i].offset = 0;
        proc->fd_table[i].flags = 0;
    }
    /* Mark 0, 1, 2 as in use for standard I/O */
    proc->fd_table[0].in_use = 1;  /* stdin - keyboard */
    proc->fd_table[1].in_use = 1;  /* stdout - serial/console */
    proc->fd_table[2].in_use = 1;  /* stderr - serial/console */
    
    uint64_t stack_top = (uint64_t)proc->stack + proc->stack_size;
    stack_top &= ~0xF;
    
    for (int i = 0; i < 16; i++) {
        ((uint64_t *)&proc->context)[i] = 0;
    }
    
    proc->context.rsp = stack_top;
    proc->context.rip = (uint64_t)process_start;
    proc->context.rflags = 0x202;
    proc->context.cs = 0x08;
    proc->context.ss = 0x10;
    proc->context.ds = 0x10;
    proc->context.es = 0x10;
    proc->context.fs = 0x10;
    proc->context.gs = 0x10;
    
    proc->context.rdi = (uint64_t)arg;
    
    if (!ready_list) {
        ready_list = proc;
    } else {
        process_t *last = ready_list;
        while (last->next) last = last->next;
        last->next = proc;
    }
    
    serial_write("[PROCESS] Created kernel process PID ");
    serial_write_dec(proc->pid);
    serial_write("\n");
    
    return proc;
}

/* ====================================================================
 * process_create_elf — Create a new user process from an ELF binary
 * ==================================================================== */
process_t *process_create_elf(const uint8_t *elf_data, size_t elf_size) {
    process_t *proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_UNUSED) {
            proc = &processes[i];
            break;
        }
    }
    
    if (!proc) {
        serial_write("[PROCESS] ERROR: Process table full!\n");
        return NULL;
    }

    #define HHDM_OFFSET 0xffff800000000000ULL
    proc->pml4 = (uint64_t *)pmm_alloc(1);
    if (!proc->pml4) return NULL;
    
    uint64_t *pml4_virt_ptr = (uint64_t *)((uint64_t)proc->pml4 + HHDM_OFFSET);
    for (int i = 0; i < 512; i++) {
        pml4_virt_ptr[i] = 0;
    }
    
    extern uint64_t *initial_pml4;
    vmm_clone_user_space(initial_pml4, pml4_virt_ptr);

    elf_info_t elf_info;
    /* Temporarily switch to new PML4 to load ELF into it */
    uint64_t *old_pml4 = kernel_pml4;
    vmm_switch_pml4(pml4_virt_ptr);
    
    if (elf_load(elf_data, elf_size, &elf_info) != 0) {
        vmm_switch_pml4(old_pml4);
        pmm_free(proc->pml4, 1);
        serial_write("[PROCESS] ERROR: Failed to load ELF\n");
        return NULL;
    }
    
    /* Switch back to kernel PML4 */
    vmm_switch_pml4(old_pml4);

    proc->stack_size = DEFAULT_STACK_SIZE;
    proc->stack = kmalloc(proc->stack_size);
    if (!proc->stack) {
        vmm_free_user_space(pml4_virt_ptr);
        pmm_free(proc->pml4, 1);
        return NULL;
    }

    proc->pid = next_pid++;
    proc->state = PROCESS_STATE_READY;
    proc->exit_code = 0;
    proc->total_ticks = 0;
    proc->time_slice = PROCESS_TIME_SLICE;
    proc->parent = current_process;
    proc->next = NULL;

    uint64_t stack_top = (uint64_t)proc->stack + proc->stack_size;
    stack_top &= ~0xF;

    for (int i = 0; i < 16; i++) {
        ((uint64_t *)&proc->context)[i] = 0;
    }

    proc->context.rsp = stack_top; /* Kernel stack for syscall entry */
    proc->context.rip = elf_info.entry_point;
    proc->context.rflags = 0x202;
    
    proc->context.cs = 0x1B;
    proc->context.ss = 0x23;
    proc->context.ds = 0x23;
    proc->context.es = 0x23;
    proc->context.fs = 0x23;
    proc->context.gs = 0x23;

    if (ready_list) {
        process_t *last = ready_list;
        while (last->next) last = last->next;
        last->next = proc;
    } else {
        ready_list = proc;
    }

    serial_write("[PROCESS] Created user process PID ");
    serial_write_dec(proc->pid);
    serial_write(" with entry point 0x");
    serial_write_hex(proc->context.rip);
    serial_write("\n");

    return proc;
}

/* ====================================================================
 * process_start — Entry point for new processes
 * ==================================================================== */
void process_start(void) {
    __asm__ volatile("sti");
    process_exit(0);
}

/* ====================================================================
 * process_current — Get currently running process
 * ==================================================================== */
process_t *process_current(void) {
    return current_process;
}

/* ====================================================================
 * scheduler — Round-robin scheduler
 * ==================================================================== */
void scheduler(void) {
    if (!ready_list) return;
    
    if (!current_process) {
        process_t *next = ready_list;
        ready_list = ready_list->next;
        next->next = NULL;
        next->time_slice = PROCESS_TIME_SLICE;
        current_process = next;
        current_process->state = PROCESS_STATE_RUNNING;
        process_switch_context(NULL, current_process);
        return;
    }
    
    if (current_process->time_slice > 0) {
        current_process->time_slice--;
        current_process->total_ticks++;
        return;
    }
    
    current_process->state = PROCESS_STATE_READY;
    current_process->time_slice = PROCESS_TIME_SLICE;
    
    if (ready_list) {
        process_t *last = ready_list;
        while (last->next) last = last->next;
        last->next = current_process;
    } else {
        ready_list = current_process;
    }
    current_process->next = NULL;
    
    process_t *next = ready_list;
    ready_list = ready_list->next;
    next->next = NULL;
    next->time_slice = PROCESS_TIME_SLICE;
    
    process_t *old = current_process;
    current_process = next;
    current_process->state = PROCESS_STATE_RUNNING;
    
    process_switch_context(old, current_process);
}

/* ====================================================================
 * process_yield — Voluntarily yield CPU
 * ==================================================================== */
void process_yield(void) {
    if (current_process) {
        current_process->time_slice = 0;
    }
    __asm__ volatile("int $0x20");
}

/* ====================================================================
 * process_exit — Exit current process
 * ==================================================================== */
void process_exit(int code) {
    if (!current_process) return;
    
    current_process->state = PROCESS_STATE_ZOMBIE;
    current_process->exit_code = code;
    
    if (current_process->stack) {
        kfree(current_process->stack);
        current_process->stack = NULL;
    }
    
    if (current_process->pml4) {
        #define HHDM_OFFSET 0xffff800000000000ULL
        uint64_t *pml4_virt = (uint64_t *)((uint64_t)current_process->pml4 + HHDM_OFFSET);
        vmm_free_user_space(pml4_virt);
        pmm_free(current_process->pml4, 1);
        current_process->pml4 = NULL;
    }
    
    if (ready_list == current_process) {
        ready_list = ready_list->next;
    } else {
        process_t *prev = ready_list;
        while (prev && prev->next != current_process) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = current_process->next;
        }
    }
    
    current_process->state = PROCESS_STATE_UNUSED;
    current_process->pid = 0;
    
    scheduler();
    
    for (;;) __asm__ volatile("hlt");
}

/* ====================================================================
 * scheduler_tick — Called from timer interrupt (IRQ0)
 * ==================================================================== */
void scheduler_tick(void) {
    if (current_process) {
        current_process->total_ticks++;
        scheduler();
    }
}

/* ====================================================================
 * process_switch_context — Assembly context switch
 * ==================================================================== */
void process_switch_context(process_t *old, process_t *new) {
    extern void switch_context(process_t *old, process_t *new);
    switch_context(old, new);
}

/* ====================================================================
 * process_block — Block current process on a wait queue
 * ==================================================================== */
void process_block(wait_queue_t *wq) {
    if (!current_process) return;
    
    current_process->state = PROCESS_STATE_BLOCKED;
    current_process->next = NULL;
    
    if (wq->tail) {
        wq->tail->next = current_process;
        wq->tail = current_process;
    } else {
        wq->head = current_process;
        wq->tail = current_process;
    }
    
    /* Force a context switch */
    scheduler();
}

/* ====================================================================
 * process_wake_all — Wake all processes on a wait queue
 * ==================================================================== */
void process_wake_all(wait_queue_t *wq) {
    if (!wq->head) return;
    
    process_t *proc = wq->head;
    while (proc) {
        process_t *next = proc->next;
        proc->state = PROCESS_STATE_READY;
        proc->next = NULL;
        
        /* Add to ready list */
        if (ready_list) {
            process_t *last = ready_list;
            while (last->next) last = last->next;
            last->next = proc;
        } else {
            ready_list = proc;
        }
        
        proc = next;
    }
    
    wq->head = NULL;
    wq->tail = NULL;
}