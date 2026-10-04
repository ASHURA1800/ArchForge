/* kernel/elf.c — ELF64 Loader
 * 
 * Loads ELF64 executables from memory into user virtual memory.
 * Sets up user stack with argv/envp for process start.
 */
#include "elf.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "../include/stddef.h"
#include "../include/stdint.h"
#include "../include/string.h"

/* ====================================================================
 * elf_validate_header — Validate ELF64 header
 * ==================================================================== */
int elf_validate_header(const elf64_ehdr_t *ehdr) {
    if (ehdr->ei_magic != ELF_MAGIC) {
        serial_write("[ELF] Invalid magic\n");
        return -1;
    }
    if (ehdr->ei_class != ELF_CLASS_64) {
        serial_write("[ELF] Not 64-bit\n");
        return -1;
    }
    if (ehdr->ei_data != ELF_DATA_LSB) {
        serial_write("[ELF] Not little-endian\n");
        return -1;
    }
    if (ehdr->e_type != ELF_TYPE_EXEC) {
        serial_write("[ELF] Not executable\n");
        return -1;
    }
    if (ehdr->e_machine != ELF_MACHINE_X86_64) {
        serial_write("[ELF] Not x86_64\n");
        return -1;
    }
    if (ehdr->e_phentsize != sizeof(elf64_phdr_t)) {
        serial_write("[ELF] Invalid program header size\n");
        return -1;
    }
    return 0;
}

/* ====================================================================
 * elf_load_segments — Load PT_LOAD segments
 * ==================================================================== */
int elf_load_segments(const elf64_phdr_t *phdrs, uint16_t phnum, 
                      const uint8_t *file_data, size_t file_size) {
    for (uint16_t i = 0; i < phnum; i++) {
        const elf64_phdr_t *ph = &phdrs[i];
        
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_filesz == 0) continue;
        
        /* Validate file bounds */
        if (ph->p_offset + ph->p_filesz > file_size) {
            serial_write("[ELF] Segment exceeds file size\n");
            return -1;
        }
        
        /* Allocate physical pages */
        size_t pages = (ph->p_memsz + 4095) / 4096;
        void *phys = pmm_alloc(pages);
        if (!phys) {
            serial_write("[ELF] Out of physical memory\n");
            return -1;
        }
        
        /* Map virtual to physical */
        uint64_t vaddr = ph->p_vaddr;
        uint64_t paddr = (uint64_t)phys;
        
        for (size_t p = 0; p < pages; p++) {
            uint64_t page_vaddr = vaddr + p * 4096;
            uint64_t page_paddr = paddr + p * 4096;
            
            uint64_t flags = PTE_PRESENT | PTE_USER;
            if (ph->p_flags & PF_W) flags |= PTE_WRITABLE;
            if (!(ph->p_flags & PF_X)) {
                /* Note: We don't have NX bit in our PTE flags yet */
            }
            
            if (vmm_map_page(page_vaddr, page_paddr, flags) != 0) {
                serial_write("[ELF] Failed to map page\n");
                return -1;
            }
        }
        
        /* Copy file data to mapped pages via HHDM */
        extern uint64_t hhdm_offset;
        uint8_t *dst_hhdm = (uint8_t *)(paddr + hhdm_offset);
        const uint8_t *src = file_data + ph->p_offset;
        
        memcpy(dst_hhdm, src, ph->p_filesz);
        
        /* Zero BSS (mem - file size) */
        if (ph->p_memsz > ph->p_filesz) {
            size_t bss_offset = ph->p_filesz;
            size_t bss_size = ph->p_memsz - ph->p_filesz;
            memset(dst_hhdm + bss_offset, 0, bss_size);
        }
        
        serial_write("[ELF] Loaded segment: vaddr=0x");
        serial_write_hex(vaddr);
        serial_write(" filesz=0x");
        serial_write_hex(ph->p_filesz);
        serial_write(" memsz=0x");
        serial_write_hex(ph->p_memsz);
        serial_write("\n");
    }
    return 0;
}

/* ====================================================================
 * elf_setup_user_stack — Set up user stack with argv/envp
 * ==================================================================== */
void elf_setup_user_stack(uint64_t stack_top, char *const argv[], 
                          char *const envp[], uint64_t *user_rsp, 
                          uint64_t *user_rsp_end) {
    uint8_t *stack = (uint8_t *)stack_top;
    
    /* Count argc, envc */
    int argc = 0;
    while (argv && argv[argc]) argc++;
    int envc = 0;
    while (envp && envp[envc]) envc++;
    
    /* Align to 16 bytes */
    stack = (uint8_t *)(((uint64_t)stack) & ~0xF);
    
    /* Calculate string space needed */
    size_t strings_size = 0;
    for (int i = 0; i < argc; i++) strings_size += strlen(argv[i]) + 1;
    for (int i = 0; i < envc; i++) strings_size += strlen(envp[i]) + 1;
    
    /* Copy strings first (at bottom of stack area) */
    uint8_t *string_area = stack - strings_size;
    uint8_t *string_ptr = string_area;
    
    char **new_argv = (char **)(string_area - (argc + 1) * sizeof(char *));
    char **new_envp = (char **)((uint8_t *)new_argv - (envc + 1) * sizeof(char *));
    
    /* Align new_argv to 16 bytes */
    new_argv = (char **)(((uint64_t)new_argv) & ~0xF);
    new_envp = (char **)(((uint64_t)new_envp) & ~0xF);
    
    int64_t *auxv = (int64_t *)((uint8_t *)new_envp - 2 * sizeof(int64_t));
    int64_t *stack_marker = (int64_t *)((uint8_t *)auxv - sizeof(int64_t));
    
    /* Copy argv strings */
    for (int i = 0; i < argc; i++) {
        new_argv[i] = (char *)string_ptr;
        size_t len = strlen(argv[i]) + 1;
        memcpy(string_ptr, argv[i], len);
        string_ptr += len;
    }
    new_argv[argc] = NULL;
    
    /* Copy envp strings */
    for (int i = 0; i < envc; i++) {
        new_envp[i] = (char *)string_ptr;
        size_t len = strlen(envp[i]) + 1;
        memcpy(string_ptr, envp[i], len);
        string_ptr += len;
    }
    new_envp[envc] = NULL;
    
    /* auxv */
    auxv[0] = 0;  /* AT_NULL */
    auxv[1] = 0;
    
    /* argc */
    *stack_marker = argc;
    
    /* Final RSP points to argc */
    *user_rsp = (uint64_t)stack_marker;
    *user_rsp_end = (uint64_t)string_area;
}

/* ====================================================================
 * elf_load — Load ELF from memory
 * ==================================================================== */
int elf_load(const uint8_t *elf_data, size_t elf_size, elf_info_t *info) {
    serial_write("[ELF] Loading ELF binary...\n");
    
    /* Validate header */
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)elf_data;
    if (elf_validate_header(ehdr) != 0) {
        return -1;
    }
    
    /* Load segments */
    const elf64_phdr_t *phdrs = (const elf64_phdr_t *)(elf_data + ehdr->e_phoff);
    if (elf_load_segments(phdrs, ehdr->e_phnum, elf_data, elf_size) != 0) {
        return -1;
    }
    
    info->entry_point = ehdr->e_entry;
    
    /* Allocate user stack */
    void *stack_phys = pmm_alloc(USER_STACK_SIZE / 4096);
    if (!stack_phys) {
        serial_write("[ELF] Out of memory for stack\n");
        return -1;
    }
    
    uint64_t stack_virt = USER_STACK_TOP;
    uint64_t stack_paddr = (uint64_t)stack_phys;
    size_t stack_pages = USER_STACK_SIZE / 4096;
    
    for (size_t i = 0; i < stack_pages; i++) {
        if (vmm_map_page(stack_virt - (i + 1) * 4096, 
                        stack_paddr + i * 4096,
                        PTE_PRESENT | PTE_WRITABLE | PTE_USER) != 0) {
            serial_write("[ELF] Failed to map stack\n");
            return -1;
        }
    }
    
    /* Set up initial stack with empty argv/envp */
    extern uint64_t hhdm_offset;
    uint8_t *stack_hhdm = (uint8_t *)(stack_paddr + hhdm_offset + USER_STACK_SIZE);
    
    /* Minimal argc=0, argv=NULL, envp=NULL */
    int64_t *stack_marker = (int64_t *)(stack_hhdm - sizeof(int64_t));
    *stack_marker = 0;
    
    int64_t *auxv = (int64_t *)(stack_marker - 2);
    auxv[0] = 0;  /* AT_NULL */
    auxv[1] = 0;
    
    info->stack_top = (uint64_t)stack_marker - hhdm_offset + stack_paddr;
    
    serial_write("[ELF] Loaded successfully: entry=0x");
    serial_write_hex(info->entry_point);
    serial_write(" stack=0x");
    serial_write_hex(info->stack_top);
    serial_write("\n");
    
    return 0;
}