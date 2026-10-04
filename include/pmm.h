#ifndef ARCHFORGE_PMM_H
#define ARCHFORGE_PMM_H

#include <stdint.h>
#include <stddef.h>
#include "limine.h"

/* Physical Memory Manager
 * Manages 4KB physical page frames using a bitmap allocator.
 * Requires the HHDM offset from Limine to convert physical to virtual addresses. */

/* Initialize PMM using the Limine memory map and HHDM offset */
void pmm_init(uint64_t hhdm_offset, volatile struct limine_memmap_request *memmap_req);

/* Allocate 'count' contiguous physical pages. Returns physical address or NULL. */
void *pmm_alloc(size_t count);

/* Free 'count' contiguous physical pages starting at 'ptr' (physical address). */
void pmm_free(void *ptr, size_t count);

/* Query memory statistics */
uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_free_memory(void);
uint64_t pmm_get_used_memory(void);

#endif /* ARCHFORGE_PMM_H */
