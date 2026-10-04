/* user/libc.c — Minimal C library for user-space programs
 * 
 * Provides syscall wrappers, memory allocation, and printf.
 */
#include "libc.h"

/* Simple 64KB bump allocator for malloc/free */
static char heap_area[64 * 1024];
static size_t heap_offset = 0;

/* Syscall wrappers using inline assembly */
static inline int64_t syscall1(int num, int64_t arg1) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall3(int num, int64_t arg1, int64_t arg2, int64_t arg3) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall4(int num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4) {
    int64_t ret;
    register int64_t r10 __asm__("r10") = arg4;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* Syscall implementations */
void exit(int code) {
    syscall1(SYS_EXIT, code);
    /* Never returns */
    for (;;) __asm__ volatile("hlt");
}

int write(int fd, const void *buf, size_t count) {
    return (int)syscall3(SYS_WRITE, fd, (int64_t)buf, count);
}

int read(int fd, void *buf, size_t count) {
    return (int)syscall3(SYS_READ, fd, (int64_t)buf, count);
}

int open(const char *path, int flags, ...) {
    (void)flags; // Additional arguments for mode not implemented yet
    return (int)syscall3(SYS_OPEN, (int64_t)path, flags, 0);
}

int close(int fd) {
    return (int)syscall1(SYS_CLOSE, fd);
}

int getpid(void) {
    return (int)syscall1(SYS_GETPID, 0);
}

int dup2(int oldfd, int newfd) {
    return (int)syscall3(SYS_DUP2, oldfd, newfd, 0);
}

int pipe(int pipefd[2]) {
    return (int)syscall1(SYS_PIPE, (int64_t)pipefd);
}

/* Simple malloc/free using bump allocator */
void *malloc(size_t size) {
    if (size == 0) return NULL;
    
    /* Align to 16 bytes */
    size = (size + 15) & ~15;
    
    if (heap_offset + size > sizeof(heap_area)) {
        return NULL; /* Out of memory */
    }
    
    void *ptr = &heap_area[heap_offset];
    heap_offset += size;
    return ptr;
}

void free(void *ptr) {
    /* Not implemented for bump allocator - simple programs won't free much */
    (void)ptr;
}

/* Simple memcpy */
void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

/* strlen */
size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/* Simple printf implementation supporting %s, %d, %x, %% */
int printf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    
    int written = 0;
    char buf[32];
    
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (!*fmt) break;
            
            switch (*fmt) {
                case 's': {
                    const char *s = __builtin_va_arg(args, const char *);
                    if (!s) s = "(null)";
                    size_t len = strlen(s);
                    write(1, s, len);
                    written += len;
                    break;
                }
                case 'd': {
                    int n = __builtin_va_arg(args, int);
                    int neg = 0;
                    if (n < 0) {
                        neg = 1;
                        n = -n;
                    }
                    int i = 0;
                    if (n == 0) {
                        buf[i++] = '0';
                    } else {
                        while (n > 0) {
                            buf[i++] = '0' + (n % 10);
                            n /= 10;
                        }
                    }
                    int len = i;
                    if (neg) write(1, "-", 1);
                    for (int j = len - 1; j >= 0; j--) {
                        char c = buf[j];
                        write(1, &c, 1);
                    }
                    written += len + (neg ? 1 : 0);
                    break;
                }
                case 'x': {
                    unsigned int n = __builtin_va_arg(args, unsigned int);
                    int i = 0;
                    if (n == 0) {
                        buf[i++] = '0';
                    } else {
                        while (n > 0) {
                            int digit = n & 0xF;
                            buf[i++] = (digit < 10) ? '0' + digit : 'a' + (digit - 10);
                            n >>= 4;
                        }
                    }
                    for (int j = i - 1; j >= 0; j--) {
                        char c = buf[j];
                        write(1, &c, 1);
                    }
                    written += i;
                    break;
                }
                case '%': {
                    write(1, "%", 1);
                    written++;
                    break;
                }
                default:
                    write(1, "%", 1);
                    write(1, fmt, 1);
                    written += 2;
                    break;
            }
        } else {
            write(1, fmt, 1);
            written++;
        }
        fmt++;
    }
    
    __builtin_va_end(args);
    return written;
}