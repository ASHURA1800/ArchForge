#include "ramfs.h"
#include "heap.h"
#include "serial.h"
#include <stddef.h>
#include <stdint.h>

static ramfs_file_t ramfs_files[RAMFS_MAX_FILES];

void ramfs_init(void) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        ramfs_files[i].in_use = 0;
        ramfs_files[i].name[0] = '\0';
        ramfs_files[i].data = NULL;
        ramfs_files[i].size = 0;
    }
    serial_write("[RAMFS] Initialized with ");
    // Note: kprintf would be better here, but we use serial_write for simplicity
    // or assume kprintf is available. Let's use a simple counter.
    serial_write("256 slots.\n");
}

ramfs_file_t *ramfs_find(const char *name) {
    if (!name || !name[0]) return NULL;
    
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (ramfs_files[i].in_use) {
            // Simple string comparison
            int match = 1;
            for (int j = 0; j < RAMFS_NAME_MAX; j++) {
                if (ramfs_files[i].name[j] != name[j]) {
                    match = 0;
                    break;
                }
                if (name[j] == '\0') break;
            }
            if (match) return &ramfs_files[i];
        }
    }
    return NULL;
}

int ramfs_create(const char *name, const void *data, size_t size) {
    if (!name || !name[0]) return -1;
    if (size == 0 && data != NULL) return -1;
    
    // Check if file already exists
    ramfs_file_t *existing = ramfs_find(name);
    if (existing) {
        // Overwrite existing
        if (existing->data) {
            kfree(existing->data);
        }
        if (size > 0 && data) {
            existing->data = (uint8_t *)kmalloc(size);
            if (!existing->data) return -2; // Out of memory
            for (size_t i = 0; i < size; i++) {
                existing->data[i] = ((const uint8_t *)data)[i];
            }
        } else {
            existing->data = NULL;
        }
        existing->size = size;
        return 0;
    }
    
    // Find empty slot
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!ramfs_files[i].in_use) {
            // Copy name
            for (int j = 0; j < RAMFS_NAME_MAX - 1; j++) {
                ramfs_files[i].name[j] = name[j];
                if (name[j] == '\0') break;
            }
            ramfs_files[i].name[RAMFS_NAME_MAX - 1] = '\0';
            
            if (size > 0 && data) {
                ramfs_files[i].data = (uint8_t *)kmalloc(size);
                if (!ramfs_files[i].data) return -2;
                for (size_t j = 0; j < size; j++) {
                    ramfs_files[i].data[j] = ((const uint8_t *)data)[j];
                }
            } else {
                ramfs_files[i].data = NULL;
            }
            ramfs_files[i].size = size;
            ramfs_files[i].in_use = 1;
            return 0;
        }
    }
    
    return -3; // No space
}

int ramfs_read(const char *name, void *buffer, size_t max_size) {
    ramfs_file_t *file = ramfs_find(name);
    if (!file) return -1; // Not found
    
    size_t to_read = file->size < max_size ? file->size : max_size;
    if (to_read > 0 && file->data && buffer) {
        for (size_t i = 0; i < to_read; i++) {
            ((uint8_t *)buffer)[i] = file->data[i];
        }
    }
    return (int)to_read;
}

int ramfs_write(const char *name, const void *data, size_t size) {
    return ramfs_create(name, data, size); // Write is same as create (overwrite)
}

int ramfs_append(const char *name, const void *data, size_t size) {
    if (!name || !name[0] || !data || size == 0) return -1;
    
    ramfs_file_t *file = ramfs_find(name);
    if (!file) {
        // If doesn't exist, just create it
        return ramfs_create(name, data, size);
    }
    
    // Allocate new buffer
    size_t new_size = file->size + size;
    uint8_t *new_data = (uint8_t *)kmalloc(new_size);
    if (!new_data) return -2;
    
    // Copy old data
    if (file->size > 0 && file->data) {
        for (size_t i = 0; i < file->size; i++) {
            new_data[i] = file->data[i];
        }
    }
    
    // Append new data
    for (size_t i = 0; i < size; i++) {
        new_data[file->size + i] = ((const uint8_t *)data)[i];
    }
    
    // Free old and update
    if (file->data) kfree(file->data);
    file->data = new_data;
    file->size = new_size;
    
    return 0;
}

int ramfs_delete(const char *name) {
    ramfs_file_t *file = ramfs_find(name);
    if (!file) return -1;
    
    if (file->data) {
        kfree(file->data);
        file->data = NULL;
    }
    file->size = 0;
    file->in_use = 0;
    file->name[0] = '\0';
    
    return 0;
}

void ramfs_list(void) {
    serial_write("[RAMFS] Directory listing:\n");
    int count = 0;
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (ramfs_files[i].in_use) {
            serial_write("  - ");
            serial_write(ramfs_files[i].name);
            serial_write(" (");
            // Simple integer to string for size
            char size_str[32];
            size_t sz = ramfs_files[i].size;
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
            serial_write(" bytes)\n");
            count++;
        }
    }
    if (count == 0) {
        serial_write("  (empty)\n");
    }
}

size_t ramfs_size(const char *name) {
    ramfs_file_t *file = ramfs_find(name);
    if (!file) return 0;
    return file->size;
}

/* Get file size via output parameter (for shell compatibility) */
int ramfs_stat(const char *name, size_t *size_out) {
    ramfs_file_t *file = ramfs_find(name);
    if (!file) return -1;
    *size_out = file->size;
    return 0;
}
