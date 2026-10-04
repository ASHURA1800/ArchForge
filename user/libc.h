#ifndef ARCHFORGE_LIBC_H
#define ARCHFORGE_LIBC_H

#include <stdint.h>
#include <stddef.h>

/* Syscall numbers (must match kernel) */
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

/* File descriptor flags */
#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0040
#define O_TRUNC   0x0200
#define O_APPEND  0x0400

/* Stdlib functions */
void exit(int code);
int write(int fd, const void *buf, size_t count);
int read(int fd, void *buf, size_t count);
int open(const char *path, int flags, ...);
int close(int fd);
int getpid(void);
void *malloc(size_t size);
void free(void *ptr);
int printf(const char *fmt, ...);
void *memcpy(void *dest, const void *src, size_t n);
size_t strlen(const char *s);

#endif