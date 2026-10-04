#ifndef ARCHFORGE_RAMFS_H
#define ARCHFORGE_RAMFS_H

#include <stdint.h>
#include <stddef.h>

#define RAMFS_MAX_FILES 256
#define RAMFS_NAME_MAX 64

typedef struct {
    char name[RAMFS_NAME_MAX];
    uint8_t *data;
    size_t size;
    int in_use;
} ramfs_file_t;

/* Initialize RAM filesystem */
void ramfs_init(void);

/* Create a new file (overwrites if exists) */
int ramfs_create(const char *name, const void *data, size_t size);

/* Read a file into buffer */
int ramfs_read(const char *name, void *buffer, size_t max_size);

/* Write/overwrite a file */
int ramfs_write(const char *name, const void *data, size_t size);

/* Append to a file */
int ramfs_append(const char *name, const void *data, size_t size);

/* Delete a file */
int ramfs_delete(const char *name);

/* List all files */
void ramfs_list(void);

/* Get file size */
size_t ramfs_size(const char *name);

/* Find a file by name (internal helper) */
ramfs_file_t *ramfs_find(const char *name);

#endif /* ARCHFORGE_RAMFS_H */
