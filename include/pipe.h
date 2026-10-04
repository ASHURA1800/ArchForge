#ifndef ARCHFORGE_PIPE_H
#define ARCHFORGE_PIPE_H

#include <stdint.h>
#include <stddef.h>

#define PIPE_CAPACITY 4096

typedef struct {
    uint8_t buffer[PIPE_CAPACITY];
    size_t read_pos;
    size_t write_pos;
    size_t count;
    int in_use;
    int read_open;
    int write_open;
} pipe_t;

#define MAX_PIPES 64

void pipe_init(void);
int pipe_create(int *pipefd);
int pipe_read(int fd, void *buf, size_t count);
int pipe_write(int fd, const void *buf, size_t count);
int pipe_close(int fd);

#endif /* ARCHFORGE_PIPE_H */