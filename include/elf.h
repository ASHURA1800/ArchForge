#ifndef ARCHFORGE_ELF_H
#define ARCHFORGE_ELF_H

#include <stdint.h>
#include <stddef.h>

/* ELF identification */
#define ELF_MAGIC 0x464C457F  /* "\x7FELF" */
#define ELF_CLASS_64 2
#define ELF_DATA_LSB 1
#define ELF_VERSION 1
#define ELF_OSABI_NONE 0
#define ELF_TYPE_EXEC 2
#define ELF_MACHINE_X86_64 0x3E

/* ELF header */
typedef struct {
    uint32_t ei_magic;
    uint8_t  ei_class;
    uint8_t  ei_data;
    uint8_t  ei_version;
    uint8_t  ei_osabi;
    uint8_t  ei_abiversion;
    uint8_t  ei_pad[7];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

/* Program header */
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

/* Program header types */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_TLS     7

/* Program header flags */
#define PF_X 1
#define PF_W 2
#define PF_R 4

/* Section header */
typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} __attribute__((packed)) elf64_shdr_t;

/* User stack initial layout */
#define USER_STACK_TOP  0x00007FFFFFFF0000ULL  /* Top of user stack */
#define USER_STACK_SIZE (1024 * 1024)  /* 1 MB default stack */

/* ELF info returned by elf_load */
typedef struct {
    uint64_t entry_point;
    uint64_t stack_top;
} elf_info_t;

/* ELF loader functions */
int elf_load(const uint8_t *elf_data, size_t elf_size, elf_info_t *info);
int elf_validate_header(const elf64_ehdr_t *ehdr);
int elf_load_segments(const elf64_phdr_t *phdrs, uint16_t phnum, const uint8_t *file_data, size_t file_size);
void elf_setup_user_stack(uint64_t stack_top, char *const argv[], char *const envp[], uint64_t *user_rsp, uint64_t *user_rsp_end);

#endif