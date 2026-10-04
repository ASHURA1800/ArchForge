#include "libc.h"

/* --- Syscall Wrappers --- */
static inline long syscall6(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(a4), "r"(a5), "r"(a6)
        : "rcx", "r11", "memory"
    );
    return ret;
}

void exit(int code) {
    syscall6(SYS_EXIT, code, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

int getpid(void) { return syscall6(SYS_GETPID, 0, 0, 0, 0, 0, 0); }
int fork(void) { return syscall6(SYS_FORK, 0, 0, 0, 0, 0, 0); }
int yield(void) { return syscall6(SYS_YIELD, 0, 0, 0, 0, 0, 0); }
int sleep_ms(int ms) { return syscall6(SYS_SLEEP_MS, ms, 0, 0, 0, 0, 0); }
int kill(int pid, int sig) { return syscall6(SYS_KILL, pid, sig, 0, 0, 0, 0); }

int exec(const char *path, char *const argv[], char *const envp[]) {
    return syscall6(SYS_EXEC, (long)path, (long)argv, (long)envp, 0, 0, 0);
}

int waitpid(int pid, int *status, int options) {
    return syscall6(SYS_WAITPID, pid, (long)status, options, 0, 0, 0);
}

int write(int fd, const void *buf, size_t count) {
    return syscall6(SYS_WRITE, fd, (long)buf, count, 0, 0, 0);
}

int read(int fd, void *buf, size_t count) {
    return syscall6(SYS_READ, fd, (long)buf, count, 0, 0, 0);
}

int open(const char *path, int flags, ...) {
    // mode is ignored for now, but kept for variadic compatibility
    return syscall6(SYS_OPEN, (long)path, flags, 0, 0, 0, 0);
}

int close(int fd) { return syscall6(SYS_CLOSE, fd, 0, 0, 0, 0, 0); }
int dup2(int oldfd, int newfd) { return syscall6(SYS_DUP2, oldfd, newfd, 0, 0, 0, 0); }
int pipe(int pipefd[2]) { return syscall6(SYS_PIPE, (long)pipefd, 0, 0, 0, 0, 0); }

/* --- Memory Management (Bump Allocator) --- */
#define HEAP_SIZE (64 * 1024)
static char heap[HEAP_SIZE];
static size_t heap_pos = 0;

void *malloc(size_t size) {
    // Align to 8 bytes
    size = (size + 7) & ~7;
    if (heap_pos + size > HEAP_SIZE) return 0;
    void *ptr = &heap[heap_pos];
    heap_pos += size;
    return ptr;
}

void free(void *ptr) {
    // Simple bump allocator: free is a no-op, or we could implement a free list.
    // For now, it's a no-op to prevent crashes.
    (void)ptr;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    for (size_t i = 0; i < n; i++) p[i] = (unsigned char)c;
    return s;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

/* --- Formatted Output --- */
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    size_t i = 0;
    while (*fmt && i < size - 1) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 's') {
                const char *s = va_arg(ap, const char *);
                while (*s && i < size - 1) { str[i++] = *s++; }
            } else if (*fmt == 'd') {
                int n = va_arg(ap, int);
                if (n < 0) { if (i < size - 1) str[i++] = '-'; n = -n; }
                char buf[12]; int j = 0;
                do { buf[j++] = (n % 10) + '0'; n /= 10; } while (n > 0);
                while (j > 0 && i < size - 1) str[i++] = buf[--j];
            } else if (*fmt == 'x') {
                unsigned int n = va_arg(ap, unsigned int);
                char buf[9]; int j = 0;
                const char *hex = "0123456789abcdef";
                do { buf[j++] = hex[n % 16]; n /= 16; } while (n > 0);
                while (j > 0 && i < size - 1) str[i++] = buf[--j];
            } else if (*fmt == '%') {
                if (i < size - 1) str[i++] = '%';
            }
        } else {
            if (i < size - 1) str[i++] = *fmt;
        }
        fmt++;
    }
    str[i] = '\0';
    return i;
}

int sprintf(char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(str, 4096, fmt, ap);
    va_end(ap);
    return ret;
}

int printf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write(1, buf, len);
    return len;
}

/* --- File I/O Wrappers --- */
FILE *fopen(const char *path, const char *mode) {
    int flags = O_RDONLY;
    if (mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a') flags = O_WRONLY | O_CREAT | O_APPEND;
    else if (mode[0] == 'r' && mode[1] == '+') flags = O_RDWR;
    
    int fd = open(path, flags);
    if (fd < 0) return 0;
    
    FILE *f = malloc(sizeof(FILE));
    if (!f) { close(fd); return 0; }
    
    f->fd = fd;
    f->buffer = malloc(512);
    f->buf_size = 512;
    f->buf_pos = 0;
    f->buf_len = 0;
    f->mode = (flags & O_WRONLY) ? 1 : 0;
    return f;
}

int fclose(FILE *stream) {
    if (!stream) return -1;
    if (stream->fd >= 3) {
        fflush(stream);
        close(stream->fd);
    }
    free(stream->buffer);
    free(stream);
    return 0;
}

int fflush(FILE *stream) {
    if (!stream || stream->mode == 0 || stream->buf_pos == 0) return 0;
    int ret = write(stream->fd, stream->buffer, stream->buf_pos);
    stream->buf_pos = 0;
    return ret;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    size_t read_bytes = 0;
    char *out = ptr;
    
    while (read_bytes < total) {
        if (stream->buf_pos >= stream->buf_len) {
            stream->buf_len = read(stream->fd, stream->buffer, stream->buf_size);
            stream->buf_pos = 0;
            if (stream->buf_len <= 0) break;
        }
        size_t to_copy = stream->buf_len - stream->buf_pos;
        if (to_copy > total - read_bytes) to_copy = total - read_bytes;
        memcpy(out + read_bytes, stream->buffer + stream->buf_pos, to_copy);
        stream->buf_pos += to_copy;
        read_bytes += to_copy;
    }
    return read_bytes / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    size_t written = 0;
    const char *in = ptr;
    
    while (written < total) {
        if (stream->buf_pos + (total - written) > stream->buf_size) {
            size_t to_fill = stream->buf_size - stream->buf_pos;
            memcpy(stream->buffer + stream->buf_pos, in + written, to_fill);
            stream->buf_pos += to_fill;
            fflush(stream);
            written += to_fill;
        } else {
            memcpy(stream->buffer + stream->buf_pos, in + written, total - written);
            stream->buf_pos += total - written;
            written = total;
        }
    }
    return written / size;
}

/* --- Environment Variables --- */
char **environ = 0;

char *getenv(const char *name) {
    if (!environ) return 0;
    size_t namelen = strlen(name);
    for (int i = 0; environ[i] != 0; i++) {
        if (strcmp(environ[i], name) == 0 || 
           (strncmp(environ[i], name, namelen) == 0 && environ[i][namelen] == '=')) {
            return environ[i] + namelen + 1;
        }
    }
    return 0;
}

int setenv(const char *name, const char *value, int overwrite) {
    // Simplified: just a placeholder for shell integration
    (void)name; (void)value; (void)overwrite;
    return 0;
}