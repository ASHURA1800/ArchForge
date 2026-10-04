/* kernel/pmm.c — Physical Memory Manager
 *
 * Bitmap-based physical page frame allocator.
 * Each bit represents one 4KB physical page:
 *   0 = free, 1 = used
 *
 * For 4GB RAM: 4GB / 4KB = 1,048,576 pages = 1M bits = 128KB bitmap
 *
 * The bitmap itself is placed in usable physical memory found from
 * the Limine memory map.
 */
#include "pmm.h"
#include "serial.h"
#include <stdbool.h>

/* ---- Module state ---- */
static uint64_t *pmm_bitmap = NULL;     /* Virtual address of bitmap            */
static size_t    pmm_bitmap_entries = 0;/* Number of uint64_t entries in bitmap */
static uint64_t  pmm_total_pages = 0;
static uint64_t  pmm_free_pages = 0;
static uint64_t  pmm_used_pages = 0;
static uint64_t  pmm_highest_page = 0;
static uint64_t  pmm_hhdm_offset = 0;  /* HHDM offset from Limine              */


/* ---- Helpers ---- */

/* Convert physical address to virtual using HHDM offset */
static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + pmm_hhdm_offset);
}

/* Find first zero bit in a 64-bit word. Returns bit index or -1 if all set. */
static inline int find_first_zero(uint64_t word) {
    if (word == 0xFFFFFFFFFFFFFFFFULL) return -1;
    return __builtin_ctzll(~word);
}


/* ---- Mark a single page as used ---- */
static inline void bitmap_set(uint64_t page) {
    pmm_bitmap[page / 64] |= (1ULL << (page % 64));
}

/* ---- Mark a single page as free ---- */
static inline void bitmap_clear(uint64_t page) {
    pmm_bitmap[page / 64] &= ~(1ULL << (page % 64));
}

/* ---- Check if a page is used ---- */
static inline bool bitmap_test(uint64_t page) {
    return (pmm_bitmap[page / 64] & (1ULL << (page % 64))) != 0;
}


/* ====================================================================
 * pmm_init — Initialize the physical memory manager
 * ==================================================================== */
void pmm_init(uint64_t hhdm_offset, volatile struct limine_memmap_request *memmap_req) {
    serial_write("[PMM] Initializing Physical Memory Manager...\n");

    pmm_hhdm_offset = hhdm_offset;

    /* ---- Validate inputs ---- */
    if (memmap_req == NULL || memmap_req->response == NULL) {
        serial_write("[PMM] ERROR: No memory map from Limine!\n");
        return;
    }

    /* The response pointer from Limine: check if it's already virtual.
     * Limine runs in the higher half and fills response pointers with
     * virtual addresses directly accessible in the kernel's address space.
     * We try direct access first. */
    struct limine_memmap_response *resp =
        (struct limine_memmap_response *)memmap_req->response;

    serial_write("[PMM] Memory map response at: ");
    serial_write_hex((uint64_t)resp);
    serial_write(", entry_count: ");
    serial_write_dec(resp->entry_count);
    serial_write(" entries\n");

    /* Verify we can read the structure by checking entry_count sanity */
    if (resp->entry_count == 0 || resp->entry_count > 1024) {
        serial_write("[PMM] WARNING: Suspicious entry_count, trying HHDM conversion...\n");
        /* Fallback: try HHDM conversion */
        resp = (struct limine_memmap_response *)phys_to_virt((uint64_t)memmap_req->response);
        serial_write("[PMM] HHDM-converted response at: ");
        serial_write_hex((uint64_t)resp);
        serial_write(", entry_count: ");
        serial_write_dec(resp->entry_count);
        serial_write("\n");
    }

    /* ---- Phase 1: Find highest physical address to size the bitmap ---- */
    pmm_highest_page = 0;
    for (size_t i = 0; i < resp->entry_count; i++) {
        /* The entries array contains pointers to entry structures.
         * These are also virtual addresses from Limine. */
        struct limine_memmap_entry *entry =
            (struct limine_memmap_entry *)resp->entries[i];

        uint64_t end = entry->base + entry->length;
        uint64_t end_page = (end + 4095) / 4096;
        if (end_page > pmm_highest_page) {
            pmm_highest_page = end_page;
        }

        /* Print each entry for debugging */
        serial_write("[PMM]   ");
        serial_write_hex(entry->base);
        serial_write(" - ");
        serial_write_hex(end);
        serial_write(" type=");
        serial_write_dec(entry->type);
        serial_write(" (");
        serial_write_dec(entry->length / 1024);
        serial_write(" KB)\n");
    }

    pmm_total_pages = pmm_highest_page;
    pmm_bitmap_entries = (pmm_total_pages + 63) / 64;

    serial_write("[PMM] Highest page: ");
    serial_write_dec(pmm_highest_page);
    serial_write(" (");
    serial_write_dec(pmm_highest_page * 4096 / 1024 / 1024);
    serial_write(" MB physical memory)\n");

    serial_write("[PMM] Bitmap size: ");
    serial_write_dec(pmm_bitmap_entries * 8 / 1024);
    serial_write(" KB (");
    serial_write_dec(pmm_bitmap_entries);
    serial_write(" entries)\n");

    /* ---- Phase 2: Find usable memory for the bitmap ---- */
    size_t bitmap_bytes = pmm_bitmap_entries * sizeof(uint64_t);
    uint64_t bitmap_phys = 0;

    for (size_t i = 0; i < resp->entry_count; i++) {
        struct limine_memmap_entry *entry =
            (struct limine_memmap_entry *)resp->entries[i];

        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_bytes) {
            bitmap_phys = entry->base;
            /* Reserve the bitmap space by adjusting the entry */
            entry->base   += bitmap_bytes;
            entry->length -= bitmap_bytes;
            break;
        }
    }

    if (bitmap_phys == 0) {
        serial_write("[PMM] FATAL: No usable memory for bitmap!\n");
        return;
    }

    serial_write("[PMM] Bitmap placed at phys ");
    serial_write_hex(bitmap_phys);
    serial_write("\n");

    /* Convert bitmap physical address to virtual */
    pmm_bitmap = (uint64_t *)phys_to_virt(bitmap_phys);

    /* ---- Phase 3: Initialize bitmap — mark ALL pages as used (1) ---- */
    serial_write("[PMM] Initializing bitmap (all pages marked used)...\n");
    for (size_t i = 0; i < pmm_bitmap_entries; i++) {
        pmm_bitmap[i] = 0xFFFFFFFFFFFFFFFFULL;
    }

    /* ---- Phase 4: Mark usable regions as free (0) ---- */
    serial_write("[PMM] Marking usable regions as free...\n");
    for (size_t i = 0; i < resp->entry_count; i++) {
        struct limine_memmap_entry *entry =
            (struct limine_memmap_entry *)resp->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) continue;

        uint64_t start_page = (entry->base + 4095) / 4096;  /* Round up  */
        uint64_t end_page   = (entry->base + entry->length) / 4096;  /* Round down */

        for (uint64_t p = start_page; p < end_page && p < pmm_total_pages; p++) {
            bitmap_clear(p);
            pmm_free_pages++;
        }
    }

    pmm_used_pages = pmm_total_pages - pmm_free_pages;

    serial_write("[PMM] Total pages: ");
    serial_write_dec(pmm_total_pages);
    serial_write(" (");
    serial_write_dec(pmm_total_pages * 4096 / 1024 / 1024);
    serial_write(" MB)\n");

    serial_write("[PMM] Free pages:  ");
    serial_write_dec(pmm_free_pages);
    serial_write(" (");
    serial_write_dec(pmm_free_pages * 4096 / 1024 / 1024);
    serial_write(" MB)\n");

    serial_write("[PMM] Used pages:  ");
    serial_write_dec(pmm_used_pages);
    serial_write("\n");

    serial_write("[PMM] PMM initialized successfully.\n");
}


/* ====================================================================
 * pmm_alloc — Allocate 'count' contiguous physical pages
 * Returns physical address of first page, or NULL on failure.
 * ==================================================================== */
void *pmm_alloc(size_t count) {
    if (count == 0 || count > pmm_free_pages || pmm_bitmap == NULL) {
        return NULL;
    }

    /* Linear scan for 'count' contiguous free pages */
    for (size_t entry = 0; entry < pmm_bitmap_entries; entry++) {
        uint64_t word = pmm_bitmap[entry];
        if (word == 0xFFFFFFFFFFFFFFFFULL) continue; /* All used in this word */

        int first_bit = find_first_zero(word);
        if (first_bit == -1) continue;

        uint64_t start_page = (uint64_t)entry * 64 + (uint64_t)first_bit;

        /* Check if we have enough contiguous free pages from start_page */
        bool found = true;
        for (size_t i = 0; i < count; i++) {
            uint64_t page_idx = start_page + i;
            if (page_idx >= pmm_total_pages || bitmap_test(page_idx)) {
                found = false;
                break;
            }
        }

        if (found) {
            /* Mark all pages as used */
            for (size_t i = 0; i < count; i++) {
                bitmap_set(start_page + i);
            }
            pmm_free_pages -= count;
            pmm_used_pages += count;

            /* Return physical address */
            return (void *)(start_page * 4096);
        }
    }

    return NULL; /* Not enough contiguous memory */
}


/* ====================================================================
 * pmm_free — Free 'count' contiguous physical pages
 * 'ptr' can be a physical or virtual address.
 * ==================================================================== */
void pmm_free(void *ptr, size_t count) {
    if (ptr == NULL || count == 0 || pmm_bitmap == NULL) return;

    uint64_t phys = (uint64_t)ptr;

    /* If the address is in the higher half, convert to physical */
    if (phys >= pmm_hhdm_offset) {
        phys -= pmm_hhdm_offset;
    }

    uint64_t start_page = phys / 4096;

    for (size_t i = 0; i < count; i++) {
        uint64_t page_idx = start_page + i;
        if (page_idx >= pmm_total_pages) break;

        if (bitmap_test(page_idx)) {
            bitmap_clear(page_idx);
            pmm_free_pages++;
            pmm_used_pages--;
        }
    }
}


/* ====================================================================
 * Query functions
 * ==================================================================== */
uint64_t pmm_get_total_memory(void) {
    return pmm_total_pages * 4096;
}

uint64_t pmm_get_free_memory(void) {
    return pmm_free_pages * 4096;
}

uint64_t pmm_get_used_memory(void) {
    return pmm_used_pages * 4096;
}
