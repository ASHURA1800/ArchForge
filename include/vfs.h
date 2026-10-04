#ifndef ARCHFORGE_VFS_H
#define ARCHFORGE_VFS_H

#include <stdint.h>
#include <stddef.h>

#define VFS_NAME_MAX 64
#define VFS_MAX_NODES 512

#define VFS_TYPE_FILE 1
#define VFS_TYPE_DIR  2

typedef struct vfs_node {
    char name[VFS_NAME_MAX];
    int type;
    uint8_t *data;
    size_t size;
    size_t capacity;
    struct vfs_node *parent;
    struct vfs_node *first_child;
    struct vfs_node *next_sibling;
    int in_use;
} vfs_node_t;

/* Initialize VFS subsystem */
void vfs_init(void);

/* Resolve a path to a vfs_node_t (e.g., "/bin/hello") */
vfs_node_t *vfs_resolve(const char *path);

/* Create a new file or directory */
vfs_node_t *vfs_create_file(const char *path, const void *data, size_t size);
vfs_node_t *vfs_create_dir(const char *path);

/* Read/Write operations */
int vfs_read(vfs_node_t *node, void *buf, size_t count, size_t offset);
int vfs_write(vfs_node_t *node, const void *buf, size_t count, size_t offset);

/* List directory contents */
void vfs_list_dir(vfs_node_t *dir);

/* Helper to get root directory */
vfs_node_t *vfs_get_root(void);

#endif /* ARCHFORGE_VFS_H */