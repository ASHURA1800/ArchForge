/* kernel/vmm.c — Virtual Memory Manager
 *
 * 4-level page table management (PML4 → PDPT → PD → PT)
 * Supports per-process page tables and Copy-on-Write (COW).
 */
#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include "string.h"

uint64_t *kernel_pml4 = NULL;  /* Virtual address of current PML4 */
uint64_t *initial_pml4 = NULL; /* Virtual address of Limine's initial PML4 */
static uint64_t hhdm_offset = 0;

/* Convert physical address to virtual using HHDM */
static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + hhdm_offset);
}

/* Convert virtual address to physical (only for HHDM-mapped addresses) */
static inline uint64_t virt_to_phys(void *virt) {
    return (uint64_t)virt - hhdm_offset;
}

/* Get or allocate the next-level table for a given entry in a specific PML4 */
static uint64_t *get_or_alloc_table(uint64_t *pml4, uint64_t *entry) {
    if (*entry & PTE_PRESENT) {
        uint64_t phys = *entry & PTE_ADDR_MASK;
        return (uint64_t *)phys_to_virt(phys);
    }

    void *new_page = pmm_alloc(1);
    if (!new_page) {
        serial_write("[VMM] FATAL: Out of memory allocating page table!\n");
        return NULL;
    }

    uint64_t *table = (uint64_t *)phys_to_virt((uint64_t)new_page);
    for (int i = 0; i < 512; i++) {
        table[i] = 0;
    }

    *entry = (uint64_t)new_page | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    return table;
}

void vmm_init(uint64_t hhdm_off) {
    serial_write("[VMM] Initializing Virtual Memory Manager...\n");
    hhdm_offset = hhdm_off;

    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    kernel_pml4 = (uint64_t *)phys_to_virt(cr3 & PTE_ADDR_MASK);
    initial_pml4 = kernel_pml4;

    serial_write("[VMM] Using Limine's PML4 at phys 0x");
    serial_write_hex(cr3 & PTE_ADDR_MASK);
    serial_write("\n");

    serial_write("[VMM] VMM initialized successfully.\n");
}

void vmm_switch_pml4(uint64_t *new_pml4) {
    kernel_pml4 = new_pml4;
    uint64_t cr3_val = virt_to_phys(new_pml4);
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3_val) : "memory");
}

int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = get_or_alloc_table(kernel_pml4, &kernel_pml4[pml4_idx]);
    if (!pdpt) return -1;

    uint64_t *pd = get_or_alloc_table(kernel_pml4, &pdpt[pdpt_idx]);
    if (!pd) return -1;

    uint64_t *pt = get_or_alloc_table(kernel_pml4, &pd[pd_idx]);
    if (!pt) return -1;

    pt[pt_idx] = (phys & PTE_ADDR_MASK) | (flags & ~PTE_ADDR_MASK);
    invlpg(virt);

    return 0;
}

int vmm_unmap_page(uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(kernel_pml4[pml4_idx] & PTE_PRESENT)) return -1;
    uint64_t *pdpt = phys_to_virt(kernel_pml4[pml4_idx] & PTE_ADDR_MASK);

    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) return -1;
    uint64_t *pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    if (!(pd[pd_idx] & PTE_PRESENT)) return -1;
    uint64_t *pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);

    pt[pt_idx] = 0;
    invlpg(virt);

    return 0;
}

uint64_t vmm_get_physical(uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(kernel_pml4[pml4_idx] & PTE_PRESENT)) return 0;
    uint64_t *pdpt = phys_to_virt(kernel_pml4[pml4_idx] & PTE_ADDR_MASK);

    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) return 0;
    uint64_t *pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    if (pd[pd_idx] & PTE_HUGE) {
        return (pd[pd_idx] & 0x000FFFFFFFE00000ULL) + (virt & 0x1FFFFF);
    }

    if (!(pd[pd_idx] & PTE_PRESENT)) return 0;
    uint64_t *pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);

    if (!(pt[pt_idx] & PTE_PRESENT)) return 0;
    return (pt[pt_idx] & PTE_ADDR_MASK) + (virt & 0xFFF);
}

/* Helper to walk page tables and return the PTE pointer */
static uint64_t *get_pte(uint64_t *pml4, uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PTE_PRESENT)) return NULL;
    uint64_t *pdpt = phys_to_virt(pml4[pml4_idx] & PTE_ADDR_MASK);

    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) return NULL;
    uint64_t *pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    if (!(pd[pd_idx] & PTE_PRESENT)) return NULL;
    uint64_t *pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);

    return &pt[pt_idx];
}

int vmm_clone_user_space(uint64_t *src_pml4, uint64_t *dst_pml4) {
    serial_write("[VMM] Cloning user space with COW...\n");
    
    /* Clone kernel space (upper half) by copying PML4 entries */
    for (uint64_t i = 256; i < 512; i++) {
        dst_pml4[i] = src_pml4[i];
    }

    /* Clone user space (lower half) with COW */
    for (uint64_t i = 0; i < 256; i++) {
        if (!(src_pml4[i] & PTE_PRESENT)) continue;
        
        uint64_t *src_pdpt = phys_to_virt(src_pml4[i] & PTE_ADDR_MASK);
        uint64_t *dst_pdpt = get_or_alloc_table(dst_pml4, &dst_pml4[i]);
        if (!dst_pdpt) return -1;

        for (uint64_t j = 0; j < 512; j++) {
            if (!(src_pdpt[j] & PTE_PRESENT)) continue;

            uint64_t *src_pd = phys_to_virt(src_pdpt[j] & PTE_ADDR_MASK);
            uint64_t *dst_pd = get_or_alloc_table(dst_pml4, &dst_pdpt[j]);
            if (!dst_pd) return -1;

            for (uint64_t k = 0; k < 512; k++) {
                if (!(src_pd[k] & PTE_PRESENT)) continue;

                uint64_t *src_pt = phys_to_virt(src_pd[k] & PTE_ADDR_MASK);
                uint64_t *dst_pt = get_or_alloc_table(dst_pml4, &dst_pd[k]);
                if (!dst_pt) return -1;

                for (uint64_t l = 0; l < 512; l++) {
                    if (!(src_pt[l] & PTE_PRESENT)) continue;

                    uint64_t pte = src_pt[l];
                    
                    /* If it's a user writable page, make it COW */
                    if ((pte & PTE_USER) && (pte & PTE_WRITABLE)) {
                        pte &= ~PTE_WRITABLE;
                        pte |= PTE_COW;
                        
                        /* Update source PTE as well */
                        src_pt[l] = pte;
                    }
                    
                    dst_pt[l] = pte;
                }
            }
        }
    }
    
    return 0;
}

void vmm_free_user_space(uint64_t *pml4) {
    serial_write("[VMM] Freeing user space...\n");
    
    for (uint64_t i = 0; i < 256; i++) {
        if (!(pml4[i] & PTE_PRESENT)) continue;
        
        uint64_t *pdpt = phys_to_virt(pml4[i] & PTE_ADDR_MASK);
        for (uint64_t j = 0; j < 512; j++) {
            if (!(pdpt[j] & PTE_PRESENT)) continue;
            
            uint64_t *pd = phys_to_virt(pdpt[j] & PTE_ADDR_MASK);
            for (uint64_t k = 0; k < 512; k++) {
                if (!(pd[k] & PTE_PRESENT)) continue;
                
                uint64_t *pt = phys_to_virt(pd[k] & PTE_ADDR_MASK);
                for (uint64_t l = 0; l < 512; l++) {
                    if (!(pt[l] & PTE_PRESENT)) continue;
                    
                    uint64_t phys = pt[l] & PTE_ADDR_MASK;
                    if (phys) {
                        pmm_free(phys_to_virt(phys), 1);
                    }
                    pt[l] = 0;
                }
                pd[k] = 0;
            }
            pdpt[j] = 0;
        }
        pml4[i] = 0;
    }
}

void handle_page_fault(uint64_t error_code, uint64_t faulting_addr) {
    serial_write("[PAGE FAULT] Error code: 0x");
    serial_write_hex(error_code);
    serial_write(" at address: 0x");
    serial_write_hex(faulting_addr);
    serial_write("\n");

    /* Check if it's a COW fault: Present (bit 0=1), Write (bit 1=1), User (bit 2=1 or 0) */
    if ((error_code & 1) && (error_code & 2)) {
        uint64_t *pte = get_pte(kernel_pml4, faulting_addr);
        if (pte && (*pte & PTE_COW)) {
            serial_write("[PAGE FAULT] Resolving COW...\n");
            
            uint64_t old_phys = *pte & PTE_ADDR_MASK;
            void *new_phys = pmm_alloc(1);
            if (!new_phys) {
                serial_write("[PAGE FAULT] FATAL: Out of memory resolving COW!\n");
                for (;;) __asm__ volatile("hlt");
            }
            
            /* Copy old page to new page */
            memcpy(phys_to_virt((uint64_t)new_phys), phys_to_virt(old_phys), 4096);
            
            /* Update PTE: remove COW, add WRITABLE */
            *pte = ((uint64_t)new_phys & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
            
            invlpg(faulting_addr);
            serial_write("[PAGE FAULT] COW resolved successfully.\n");
            return;
        }
    }

    serial_write("[PAGE FAULT] FATAL: Unhandled page fault!\n");
    for (;;) __asm__ volatile("hlt");
}