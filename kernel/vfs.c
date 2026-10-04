/* kernel/vfs.c — Virtual File System Implementation
 * 
 * Provides a hierarchical in-memory filesystem.
 */
#include "vfs.h"
#include "heap.h"
#include "serial.h"
#include "string.h"

static vfs_node_t vfs_nodes[VFS_MAX_NODES];
static vfs_node_t *root_node = NULL;

void vfs_init(void) {
    for (int i = 0; i < VFS_MAX_NODES; i++) {
        vfs_nodes[i].in_use = 0;
        vfs_nodes[i].name[0] = '\0';
        vfs_nodes[i].data = NULL;
        vfs_nodes[i].size = 0;
        vfs_nodes[i].capacity = 0;
        vfs_nodes[i].parent = NULL;
        vfs_nodes[i].first_child = NULL;
        vfs_nodes[i].next_sibling = NULL;
    }
    
    /* Allocate root node */
    root_node = &vfs_nodes[0];
    root_node->in_use = 1;
    root_node->type = VFS_TYPE_DIR;
    strcpy(root_node->name, "/");
    
    serial_write("[VFS] Initialized hierarchical filesystem.\n");
}

vfs_node_t *vfs_get_root(void) {
    return root_node;
}

/* Helper: find a child node by name */
static vfs_node_t *find_child(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return NULL;
    vfs_node_t *child = parent->first_child;
    while (child) {
        if (strcmp(child->name, name) == 0) {
            return child;
        }
        child = child->next_sibling;
    }
    return NULL;
}

/* Helper: get a free node */
static vfs_node_t *get_free_node(void) {
    for (int i = 1; i < VFS_MAX_NODES; i++) {
        if (!vfs_nodes[i].in_use) {
            return &vfs_nodes[i];
        }
    }
    return NULL;
}

vfs_node_t *vfs_resolve(const char *path) {
    if (!path || !path[0]) return NULL;
    
    /* Must start with '/' */
    if (path[0] != '/') return NULL;
    
    vfs_node_t *current = root_node;
    const char *p = path + 1;
    
    while (*p) {
        /* Skip multiple slashes */
        while (*p == '/') p++;
        if (!*p) break;
        
        /* Extract next component */
        char component[VFS_NAME_MAX];
        int i = 0;
        while (*p && *p != '/' && i < VFS_NAME_MAX - 1) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        
        /* Find child */
        vfs_node_t *child = find_child(current, component);
        if (!child) {
            return NULL; /* Not found */
        }
        current = child;
    }
    
    return current;
}

vfs_node_t *vfs_create_dir(const char *path) {
    if (!path || path[0] != '/') return NULL;
    
    /* Find parent directory */
    char parent_path[VFS_NAME_MAX];
    strcpy(parent_path, path);
    char *last_slash = strrchr(parent_path, '/');
    if (!last_slash) return NULL;
    
    if (last_slash == parent_path) {
        /* Creating directly under root */
        parent_path[1] = '\0';
    } else {
        *last_slash = '\0';
    }
    
    vfs_node_t *parent = vfs_resolve(parent_path);
    if (!parent || parent->type != VFS_TYPE_DIR) return NULL;
    
    const char *name = last_slash + 1;
    if (!name[0]) return NULL;
    
    /* Check if already exists */
    if (find_child(parent, name)) return NULL;
    
    vfs_node_t *new_node = get_free_node();
    if (!new_node) return NULL;
    
    new_node->in_use = 1;
    new_node->type = VFS_TYPE_DIR;
    strcpy(new_node->name, name);
    new_node->parent = parent;
    new_node->first_child = NULL;
    new_node->next_sibling = parent->first_child;
    parent->first_child = new_node;
    
    return new_node;
}

vfs_node_t *vfs_create_file(const char *path, const void *data, size_t size) {
    if (!path || path[0] != '/') return NULL;
    
    char parent_path[VFS_NAME_MAX];
    strcpy(parent_path, path);
    char *last_slash = strrchr(parent_path, '/');
    if (!last_slash) return NULL;
    
    if (last_slash == parent_path) {
        parent_path[1] = '\0';
    } else {
        *last_slash = '\0';
    }
    
    vfs_node_t *parent = vfs_resolve(parent_path);
    if (!parent || parent->type != VFS_TYPE_DIR) return NULL;
    
    const char *name = last_slash + 1;
    if (!name[0]) return NULL;
    
    /* Check if already exists, if so, overwrite */
    vfs_node_t *existing = find_child(parent, name);
    if (existing) {
        if (existing->type != VFS_TYPE_FILE) return NULL;
        if (existing->data) {
            kfree(existing->data);
        }
        if (size > 0 && data) {
            existing->data = (uint8_t *)kmalloc(size);
            if (!existing->data) return NULL;
            memcpy(existing->data, data, size);
        } else {
            existing->data = NULL;
        }
        existing->size = size;
        existing->capacity = size;
        return existing;
    }
    
    vfs_node_t *new_node = get_free_node();
    if (!new_node) return NULL;
    
    new_node->in_use = 1;
    new_node->type = VFS_TYPE_FILE;
    strcpy(new_node->name, name);
    new_node->parent = parent;
    new_node->first_child = NULL;
    new_node->next_sibling = parent->first_child;
    parent->first_child = new_node;
    
    if (size > 0 && data) {
        new_node->data = (uint8_t *)kmalloc(size);
        if (!new_node->data) {
            new_node->in_use = 0;
            parent->first_child = new_node->next_sibling;
            return NULL;
        }
        memcpy(new_node->data, data, size);
        new_node->size = size;
        new_node->capacity = size;
    }
    
    return new_node;
}

int vfs_read(vfs_node_t *node, void *buf, size_t count, size_t offset) {
    if (!node || node->type != VFS_TYPE_FILE || !buf) return -1;
    if (offset >= node->size) return 0;
    
    size_t to_read = count;
    if (offset + to_read > node->size) {
        to_read = node->size - offset;
    }
    
    memcpy(buf, node->data + offset, to_read);
    return (int)to_read;
}

int vfs_write(vfs_node_t *node, const void *buf, size_t count, size_t offset) {
    if (!node || node->type != VFS_TYPE_FILE || !buf) return -1;
    
    size_t new_size = offset + count;
    if (new_size > node->capacity) {
        size_t new_capacity = (new_size + 4095) & ~4095; /* Round up to page */
        uint8_t *new_data = (uint8_t *)kmalloc(new_capacity);
        if (!new_data) return -1;
        
        if (node->data && node->size > 0) {
            memcpy(new_data, node->data, node->size);
        }
        if (node->data) {
            kfree(node->data);
        }
        node->data = new_data;
        node->capacity = new_capacity;
    }
    
    memcpy(node->data + offset, buf, count);
    if (new_size > node->size) {
        node->size = new_size;
    }
    
    return (int)count;
}

void vfs_list_dir(vfs_node_t *dir) {
    if (!dir || dir->type != VFS_TYPE_DIR) return;
    
    serial_write("[VFS] Directory contents of '");
    serial_write(dir->name);
    serial_write("':\n");
    
    vfs_node_t *child = dir->first_child;
    if (!child) {
        serial_write("  (empty)\n");
        return;
    }
    
    while (child) {
        serial_write("  ");
        if (child->type == VFS_TYPE_DIR) {
            serial_write("[DIR] ");
        } else {
            serial_write("[FILE] ");
        }
        serial_write(child->name);
        if (child->type == VFS_TYPE_FILE) {
            serial_write(" (");
            /* Simple integer to string */
            char size_str[32];
            size_t sz = child->size;
            int idx = 31;
            size_str[idx] = '\0';
            if (sz == 0) {
                size_str[--idx] = '0';
            } else {
                while (sz > 0 && idx > 0) {
                    size_str[--idx] = '0' + (sz % 10);
                    sz /= 10;
                }
            }
            serial_write(&size_str[idx]);
            serial_write(" bytes)");
        }
        serial_write("\n");
        child = child->next_sibling;
    }
}