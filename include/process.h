#ifndef ARCHFORGE_PROCESS_H
#define ARCHFORGE_PROCESS_H

#include <stdint.h>
#include <stddef.h>

/* Process states */
typedef enum {
    PROCESS_STATE_UNUSED = 0,
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
    PROCESS_STATE_ZOMBIE
} process_state_t;

/* File descriptor table constants */
#define MAX_FDS 16

/* File descriptor entry */
typedef struct {
    void *node;      /* vfs_node_t* */
    size_t offset;
    int flags;
    int in_use;
} fd_entry_t;

/* Process Control Block (PCB) */
typedef struct process {
    uint64_t pid;
    process_state_t state;
    
    /* Page table for this process */
    uint64_t *pml4;
    
    /* Register context for context switching */
    struct {
        uint64_t rax, rbx, rcx, rdx;
        uint64_t rsi, rdi, rbp, rsp;
        uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
        uint64_t rip;
        uint64_t rflags;
        uint64_t cs, ss, ds, es, fs, gs;
    } context;
    
    /* Stack */
    void *stack;
    size_t stack_size;
    
    /* Scheduling */
    uint64_t time_slice;      /* Time slice remaining (ms) */
    uint64_t total_ticks;     /* Total CPU time used */
    
    /* Linked list for scheduler */
    struct process *next;
    
    /* Exit code */
    int exit_code;
    
    /* Parent process */
    struct process *parent;
    
    /* File descriptor table */
    fd_entry_t fd_table[MAX_FDS];
} process_t;

/* Maximum number of processes */
#define MAX_PROCESSES 64

/* Wait queue for blocking processes */
typedef struct wait_queue {
    process_t *head;
    process_t *tail;
} wait_queue_t;

/* Time slice for round-robin (ms) */
#define PROCESS_TIME_SLICE 10

/* Global variables (defined in process.c) */
extern process_t *current_process;
extern process_t *ready_list;
extern uint64_t next_pid;

/* Process management functions */
void process_init(void);
process_t *process_create(void (*entry)(void), void *arg);
process_t *process_create_elf(const uint8_t *elf_data, size_t elf_size);
void process_yield(void);
void process_exit(int code);
void scheduler(void);

/* Get current process */
process_t *process_current(void);

/* Timer callback - called from IRQ0 handler */
void scheduler_tick(void);

/* Blocking and waking */
void process_block(wait_queue_t *wq);
void process_wake_all(wait_queue_t *wq);

#endif