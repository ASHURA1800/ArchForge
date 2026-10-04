#ifndef USER_LIBC_H
#define USER_LIBC_H

#include <stdint.h>
#include <stddef.h>

/* Syscall wrappers */
int open(const char *path, int flags);
int read(int fd, void *buf, size_t count);
int write(int fd, const void *buf, size_t count);
int close(int fd);
void exit(int code);
int getpid(void);

/* Standard library */
void printf(const char *fmt, ...);
void *malloc(size_t size);
void free(void *ptr);
size_t strlen(const char *s);
void *memcpy(void *dest, const void *src, size_t n);

#endif
