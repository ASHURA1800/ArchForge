/* kernel/pipe.c — Anonymous Pipe Implementation
 * 
 * Provides inter-process communication via pipes.
 */
#include "pipe.h"
#include "heap.h"
#include "serial.h"
#include "string.h"

static pipe_t pipes[MAX_PIPES];

void pipe_init(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        pipes[i].in_use = 0;
        pipes[i].read_pos = 0;
        pipes[i].write_pos = 0;
        pipes[i].count = 0;
        pipes[i].read_open = 0;
        pipes[i].write_open = 0;
    }
    serial_write("[PIPE] Initialized pipe subsystem.\n");
}

/* Allocate a new pipe and return read/write file descriptors */
int pipe_create(int *pipefd) {
    if (!pipefd) return -1;
    
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].in_use) {
            pipes[i].in_use = 1;
            pipes[i].read_pos = 0;
            pipes[i].write_pos = 0;
            pipes[i].count = 0;
            pipes[i].read_open = 1;
            pipes[i].write_open = 1;
            
            /* Use negative FDs to distinguish from regular files */
            /* We'll use -1 for read, -2 for write, etc. */
            /* Actually, let's just use a global FD counter or return specific values */
            /* For simplicity, pipefd[0] = -(i * 2 + 1), pipefd[1] = -(i * 2 + 2) */
            pipefd[0] = -(i * 2 + 1);
            pipefd[1] = -(i * 2 + 2);
            
            return 0;
        }
    }
    return -1; /* No free pipes */
}

/* Helper to get pipe from FD */
static pipe_t *get_pipe_from_fd(int fd, int is_read) {
    if (fd >= 0) return NULL;
    
    int abs_fd = -fd;
    int pipe_idx = (abs_fd - 1) / 2;
    int type = (abs_fd - 1) % 2; /* 0 = read, 1 = write */
    
    if (pipe_idx < 0 || pipe_idx >= MAX_PIPES) return NULL;
    if (!pipes[pipe_idx].in_use) return NULL;
    
    if (is_read && type != 0) return NULL;
    if (!is_read && type != 1) return NULL;
    
    return &pipes[pipe_idx];
}

int pipe_read(int fd, void *buf, size_t count) {
    pipe_t *pipe = get_pipe_from_fd(fd, 1);
    if (!pipe || !buf || count == 0) return -1;
    
    if (!pipe->read_open) return -1;
    
    size_t to_read = count;
    if (to_read > pipe->count) {
        to_read = pipe->count;
    }
    
    if (to_read == 0) {
        return 0; /* EOF if write end is closed, but we don't track that fully yet */
    }
    
    size_t read_from_buffer = 0;
    uint8_t *dst = (uint8_t *)buf;
    
    while (read_from_buffer < to_read) {
        dst[read_from_buffer] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_CAPACITY;
        pipe->count--;
        read_from_buffer++;
    }
    
    return (int)read_from_buffer;
}

int pipe_write(int fd, const void *buf, size_t count) {
    pipe_t *pipe = get_pipe_from_fd(fd, 0);
    if (!pipe || !buf || count == 0) return -1;
    
    if (!pipe->write_open) return -1;
    
    size_t to_write = count;
    if (to_write > (PIPE_CAPACITY - pipe->count)) {
        to_write = PIPE_CAPACITY - pipe->count; /* Non-blocking: write what we can */
    }
    
    if (to_write == 0) {
        return 0; /* Pipe full */
    }
    
    size_t written = 0;
    const uint8_t *src = (const uint8_t *)buf;
    
    while (written < to_write) {
        pipe->buffer[pipe->write_pos] = src[written];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_CAPACITY;
        pipe->count++;
        written++;
    }
    
    return (int)written;
}

int pipe_close(int fd) {
    pipe_t *pipe = get_pipe_from_fd(fd, (fd < 0 && (-fd % 2) == 1));
    if (!pipe) return -1;
    
    int is_read = ((-fd) % 2) == 1;
    if (is_read) {
        pipe->read_open = 0;
    } else {
        pipe->write_open = 0;
    }
    
    if (!pipe->read_open && !pipe->write_open) {
        pipe->in_use = 0;
    }
    
    return 0;
}