#ifndef ARCHFORGE_HEAP_H
#define ARCHFORGE_HEAP_H

#include <stdint.h>
#include <stddef.h>

/* Kernel heap virtual address range */
#define HEAP_BASE   0xffffa00000000000ULL
#define HEAP_SIZE   (16 * 1024 * 1024)  /* 16 MB initial heap */

/* Initialize the kernel heap */
void heap_init(void);

/* Allocate 'size' bytes from the kernel heap */
void *kmalloc(size_t size);

/* Allocate zeroed memory */
void *kcalloc(size_t count, size_t size);

/* Reallocate memory */
void *krealloc(void *ptr, size_t new_size);

/* Free previously allocated memory */
void kfree(void *ptr);

/* Get heap statistics */
void heap_stats(uint64_t *total, uint64_t *used, uint64_t *free_mem);

#endif /* ARCHFORGE_HEAP_H */