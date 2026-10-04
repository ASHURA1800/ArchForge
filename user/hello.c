/* user/hello.c — Simple user-space "Hello World" program
 * 
 * This will be compiled as a static ELF64 executable and placed in RAMFS.
 * Uses syscalls directly (no libc yet).
 */
#include <stdint.h>
#include <stddef.h>

/* Syscall numbers (must match kernel) */
#define SYS_WRITE     1
#define SYS_EXIT      0
#define SYS_GETPID    4
#define SYS_SLEEP_MS  5

/* Inline syscall wrapper */
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

/* Write to fd */
static int write(int fd, const char *buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (int64_t)buf, count);
}

/* Exit */
static void exit(int code) {
    syscall1(SYS_EXIT, code);
    /* Never returns */
    for (;;) __asm__ volatile("hlt");
}

/* Get PID */
static int getpid(void) {
    return syscall1(SYS_GETPID, 0);
}

/* Sleep */
static int sleep_ms(uint64_t ms) {
    return syscall1(SYS_SLEEP_MS, ms);
}

/* String length */
static size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/* Print string */
static void puts(const char *s) {
    write(1, s, strlen(s));
}

/* Print integer */
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

/* Entry point */
void _start(void) {
    puts("Hello from user space!\n");
    puts("PID: ");
    put_dec(getpid());
    puts("\n");
    
    puts("Sleeping 1 second...\n");
    sleep_ms(1000);
    
    puts("Done!\n");
    exit(42);
}