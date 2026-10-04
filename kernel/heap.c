/* kernel/heap.c — Kernel Heap Allocator
 *
 * Simple first-fit allocator with block coalescing on free.
 * Built on top of PMM + VMM for dynamic page allocation.
 */
#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "../include/stdbool.h"

/* Block header placed before each allocation */
typedef struct block_header {
    size_t size;                /* Size of this block (including header) */
    bool is_free;
    struct block_header *next;  /* Next block in the list */
} block_header_t;

#define HEADER_SIZE sizeof(block_header_t)
#define MIN_BLOCK_SIZE (HEADER_SIZE + 16)  /* Minimum allocation unit */

static block_header_t *heap_start = NULL;
static uint64_t heap_total = 0;
static uint64_t heap_used = 0;

/* ====================================================================
 * heap_init — Initialize kernel heap
 * ==================================================================== */
void heap_init(void) {
    serial_write("[HEAP] Initializing kernel heap at ");
    serial_write_hex(HEAP_BASE);
    serial_write(" (");
    serial_write_dec(HEAP_SIZE / 1024 / 1024);
    serial_write(" MB)\n");

    /* Allocate physical pages and map them to the heap region */
    size_t pages_needed = HEAP_SIZE / 4096;
    for (size_t i = 0; i < pages_needed; i++) {
        void *phys = pmm_alloc(1);
        if (!phys) {
            serial_write("[HEAP] FATAL: Out of physical memory for heap!\n");
            return;
        }
        uint64_t virt = HEAP_BASE + (i * 4096);
        if (vmm_map_page(virt, (uint64_t)phys,
                        PTE_PRESENT | PTE_WRITABLE) != 0) {
            serial_write("[HEAP] FATAL: Failed to map heap page!\n");
            return;
        }
    }

    /* Initialize the first block — entire heap as one large free block */
    heap_start = (block_header_t *)HEAP_BASE;
    heap_start->size = HEAP_SIZE;
    heap_start->is_free = true;
    heap_start->next = NULL;

    heap_total = HEAP_SIZE;
    heap_used = 0;

    serial_write("[HEAP] Heap initialized successfully.\n");
}

/* ====================================================================
 * find_free_block — First-fit search for a free block >= size
 * ==================================================================== */
static block_header_t *find_free_block(size_t size) {
    block_header_t *current = heap_start;
    while (current) {
        if (current->is_free && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/* ====================================================================
 * split_block — Split a block if it's large enough
 * ==================================================================== */
static void split_block(block_header_t *block, size_t size) {
    if (block->size < size + MIN_BLOCK_SIZE) {
        return;  /* Not enough room to split */
    }

    /* Create new block after the allocated portion */
    block_header_t *new_block = (block_header_t *)((uint8_t *)block + size);
    new_block->size = block->size - size;
    new_block->is_free = true;
    new_block->next = block->next;

    block->size = size;
    block->next = new_block;
}

/* ====================================================================
 * coalesce_free_blocks — Merge adjacent free blocks
 * ==================================================================== */
static void coalesce_free_blocks(void) {
    block_header_t *current = heap_start;
    while (current && current->next) {
        if (current->is_free && current->next->is_free) {
            current->size += current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}

/* ====================================================================
 * kmalloc — Allocate 'size' bytes from the kernel heap
 * ==================================================================== */
void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Align size to 8 bytes */
    size = (size + 7) & ~7;
    size += HEADER_SIZE;  /* Include header */

    block_header_t *block = find_free_block(size);
    if (!block) {
        return NULL;  /* Out of memory */
    }

    /* Split if possible */
    split_block(block, size);

    block->is_free = false;
    heap_used += block->size;

    /* Return pointer to payload (after header) */
    return (uint8_t *)block + HEADER_SIZE;
}

/* ====================================================================
 * kcalloc — Allocate zeroed memory
 * ==================================================================== */
void *kcalloc(size_t count, size_t size) {
    size_t total = count * size;
    void *ptr = kmalloc(total);
    if (ptr) {
        for (size_t i = 0; i < total; i++) {
            ((uint8_t *)ptr)[i] = 0;
        }
    }
    return ptr;
}

/* ====================================================================
 * kfree — Free previously allocated memory
 * ==================================================================== */
void kfree(void *ptr) {
    if (!ptr) return;

    block_header_t *block = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);

    if (!block->is_free) {
        block->is_free = true;
        heap_used -= block->size;
    }

    /* Coalesce adjacent free blocks */
    coalesce_free_blocks();
}

/* ====================================================================
 * krealloc — Reallocate memory
 * ==================================================================== */
void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    block_header_t *block = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);
    size_t old_payload = block->size - HEADER_SIZE;

    if (new_size <= old_payload) {
        /* Shrinking — try to split */
        size_t aligned = (new_size + 7) & ~7;
        split_block(block, aligned + HEADER_SIZE);
        return ptr;
    }

    /* Growing — allocate new, copy, free old */
    void *new_ptr = kmalloc(new_size);
    if (new_ptr) {
        for (size_t i = 0; i < old_payload; i++) {
            ((uint8_t *)new_ptr)[i] = ((uint8_t *)ptr)[i];
        }
        kfree(ptr);
    }
    return new_ptr;
}

/* ====================================================================
 * heap_stats — Get heap statistics
 * ==================================================================== */
void heap_stats(uint64_t *total, uint64_t *used, uint64_t *free_mem) {
    if (total) *total = heap_total;
    if (used) *used = heap_used;
    if (free_mem) *free_mem = heap_total - heap_used;
}