#ifndef ARCHFORGE_LIBC_H
#define ARCHFORGE_LIBC_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

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
#define SYS_DUP2      16
#define SYS_PIPE      17
#define SYS_KILL      18
#define SYS_SIGNAL    19
#define SYS_SHMGET    20
#define SYS_SHMAT     21

/* File descriptor flags */
#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0040
#define O_TRUNC   0x0200
#define O_APPEND  0x0400

/* Signal numbers */
#define SIGINT  2
#define SIGTERM 15
#define SIGCHLD 17

/* Process functions */
void exit(int code) __attribute__((noreturn));
int getpid(void);
int fork(void);
int exec(const char *path, char *const argv[], char *const envp[]);
int waitpid(int pid, int *status, int options);
int kill(int pid, int sig);
int yield(void);
int sleep_ms(int ms);

/* I/O functions */
int write(int fd, const void *buf, size_t count);
int read(int fd, void *buf, size_t count);
int open(const char *path, int flags, ...);
int close(int fd);
int dup2(int oldfd, int newfd);
int pipe(int pipefd[2]);

/* File I/O wrappers (Phase 21) */
typedef struct {
    int fd;
    char *buffer;
    size_t buf_size;
    size_t buf_pos;
    size_t buf_len;
    int mode; // 0=read, 1=write
} FILE;

#define stdin  ((FILE *)0)
#define stdout ((FILE *)1)
#define stderr ((FILE *)2)

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush(FILE *stream);

/* Memory functions */
void *malloc(size_t size);
void free(void *ptr);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
char *strcpy(char *dest, const char *src);

/* String & Print functions */
int printf(const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

/* Environment (Phase 28) */
extern char **environ;
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);

#endif