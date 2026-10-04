#ifndef ARCHFORGE_VMM_H
#define ARCHFORGE_VMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Page Table Entry flags (bits 0-11) */
#define PTE_PRESENT    (1ULL << 0)   /* Page is present in memory */
#define PTE_WRITABLE   (1ULL << 1)   /* Page is writable */
#define PTE_USER       (1ULL << 2)   /* Page accessible from ring 3 */
#define PTE_PWT        (1ULL << 3)   /* Page-level write-through */
#define PTE_PCD        (1ULL << 4)   /* Page-level cache disable */
#define PTE_ACCESSED   (1ULL << 5)   /* CPU sets this on access */
#define PTE_DIRTY      (1ULL << 6)   /* CPU sets this on write */
#define PTE_HUGE       (1ULL << 7)   /* 2MB/1GB page (PD/PDPT level) */
#define PTE_GLOBAL     (1ULL << 8)   /* Global page (not flushed on CR3 write) */
#define PTE_COW        (1ULL << 9)   /* Software: Copy-on-Write page */
#define PTE_NX         (1ULL << 63)  /* No-execute (requires NXE bit in IA32_EFER) */

/* Address mask: bits 12-51 contain the physical address */
#define PTE_ADDR_MASK  0x000FFFFFFFFFF000ULL

/* User space boundary (upper half is kernel) */
#define USER_SPACE_MAX 0x00007FFFFFFFFFFFULL

/* Initialize VMM using Limine's existing page tables */
void vmm_init(uint64_t hhdm_offset);

/* Map a virtual page to a physical page with given flags in current PML4 */
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);

/* Unmap a virtual page in current PML4 */
int vmm_unmap_page(uint64_t virt);

/* Get the physical address mapped to a virtual address (0 if not mapped) */
uint64_t vmm_get_physical(uint64_t virt);

/* Invalidate TLB entry for a virtual address */
static inline void invlpg(uint64_t addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

/* Flush entire TLB by reloading CR3 */
static inline void flush_tlb(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

/* Switch to a new page table (updates CR3 and global kernel_pml4) */
void vmm_switch_pml4(uint64_t *new_pml4);

/* Clone user space from src_pml4 to dst_pml4 with COW */
int vmm_clone_user_space(uint64_t *src_pml4, uint64_t *dst_pml4);

/* Free all user space mappings in the given PML4 */
void vmm_free_user_space(uint64_t *pml4);

/* Handle page fault */
void handle_page_fault(uint64_t error_code, uint64_t faulting_addr);

/* Map MMIO region (uncached, user or kernel) */
int vmm_map_mmio(uint64_t virt, uint64_t phys, size_t size, uint64_t flags);

#endif /* ARCHFORGE_VMM_H */