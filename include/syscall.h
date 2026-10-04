#ifndef ARCHFORGE_SYSCALL_H
#define ARCHFORGE_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* Type definitions */
typedef int64_t off_t;
typedef int32_t pid_t;

/* Syscall numbers */
#define SYS_EXIT      0
#define SYS_WRITE     1
#define SYS_READ      2
#define SYS_YIELD     3
#define SYS_GETPID    4
#define SYS_SLEEP_MS  5
#define SYS_MMAP      6
#define SYS_MUNMAP    7
#define SYS_FORK      8
#define SYS_EXEC      9
#define SYS_WAITPID   10
#define SYS_OPEN      11
#define SYS_CLOSE     12
#define SYS_STAT      13
#define SYS_LSEEK     14
#define SYS_IOCTL     15

#define MAX_SYSCALLS 16

/* Syscall register structure (matches interrupt frame) */
struct syscall_frame {
    uint64_t rax, rdi, rsi, rdx, r10, r8, r9;
    uint64_t rcx, r11;  /* Saved by syscall/sysret */
};

/* Initialize syscall interface */
void syscall_init(void);

/* Syscall handler (called from assembly) */
void syscall_handler(void *saved_regs);

/* Syscall implementations */
int64_t sys_exit(int code);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_yield(void);
int64_t sys_getpid(void);
int64_t sys_sleep_ms(uint64_t ms);
int64_t sys_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
int64_t sys_munmap(void *addr, size_t len);
int64_t sys_fork(void);
int64_t sys_exec(const char *path, char *const argv[], char *const envp[]);
int64_t sys_waitpid(pid_t pid, int *status, int options);
int64_t sys_open(const char *path, int flags, ...);
int64_t sys_close(int fd);
int64_t sys_stat(const char *path, void *statbuf);
int64_t sys_lseek(int fd, off_t offset, int whence);
int64_t sys_ioctl(int fd, unsigned long request, ...);

#endif