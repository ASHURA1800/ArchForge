#include "libc.h"

#define SYS_EXIT      0
#define SYS_WRITE     1
#define SYS_READ      2
#define SYS_GETPID    4
#define SYS_OPEN      11
#define SYS_CLOSE     12
#define SYS_MMAP      6

static inline int64_t syscall1(int num, int64_t arg1) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall3(int num, int64_t arg1, int64_t arg2, int64_t arg3) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3) : "rcx", "r11", "memory");
    return ret;
}

int open(const char *path, int flags) {
    return syscall3(SYS_OPEN, (int64_t)path, flags, 0);
}

int read(int fd, void *buf, size_t count) {
    return syscall3(SYS_READ, fd, (int64_t)buf, count);
}

int write(int fd, const void *buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (int64_t)buf, count);
}

int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

void exit(int code) {
    syscall1(SYS_EXIT, code);
    for (;;) __asm__ volatile("hlt");
}

int getpid(void) {
    return syscall1(SYS_GETPID, 0);
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

/* Simple bump allocator for user space */
static uint8_t heap[65536];
static size_t heap_ptr = 0;

void *malloc(size_t size) {
    size = (size + 7) & ~7; /* Align to 8 bytes */
    if (heap_ptr + size > sizeof(heap)) return NULL;
    void *ptr = &heap[heap_ptr];
    heap_ptr += size;
    return ptr;
}

void free(void *ptr) {
    (void)ptr; /* Simple bump allocator doesn't support free */
}

/* Simple printf */
static void put_dec(int n) {
    char buf[16];
    int i = 0;
    if (n == 0) {
        write(1, "0", 1);
        return;
    }
    if (n < 0) {
        write(1, "-", 1);
        n = -n;
    }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        write(1, &buf[--i], 1);
    }
}

static void put_hex(uint64_t n) {
    const char *hex = "0123456789abcdef";
    char buf[16];
    int i = 0;
    if (n == 0) {
        write(1, "0", 1);
        return;
    }
    while (n > 0) {
        buf[i++] = hex[n & 0xF];
        n >>= 4;
    }
    write(1, "0x", 2);
    while (i > 0) {
        write(1, &buf[--i], 1);
    }
}

void printf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 's') {
                const char *s = __builtin_va_arg(args, const char *);
                write(1, s, strlen(s));
            } else if (*fmt == 'd') {
                int d = __builtin_va_arg(args, int);
                put_dec(d);
            } else if (*fmt == 'x') {
                uint64_t x = __builtin_va_arg(args, uint64_t);
                put_hex(x);
            } else if (*fmt == '%') {
                write(1, "%", 1);
            }
        } else {
            char c = *fmt;
            write(1, &c, 1);
        }
        fmt++;
    }
    __builtin_va_end(args);
}
