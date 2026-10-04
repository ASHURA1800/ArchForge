# PROGRESS.md — ArchForge OS Sprint A Progress

## Sprint A: FAT32 Write/Persistence

### Milestone Status

| Milestone | Status | Notes |
|-----------|--------|-------|
| A1 MBR + partition offset | ⬜ TODO | |
| A2 Block cache + FAT cache | ⬜ TODO | |
| A3 Cluster allocator | ⬜ TODO | |
| A4 Chain ops | ⬜ TODO | |
| A5 fat32_write_file | ⬜ TODO | |
| A6 Directory entry creation | ⬜ TODO | |
| A7 mkdir/unlink/rmdir/rename | ⬜ TODO | |
| A8 ATA hardening | ⬜ TODO | |
| A9 VFS integration | ⬜ TODO | |
| A10 User utilities | ⬜ TODO | |
| A11 PERSISTENCE test | ⬜ TODO | |
| A12 Robustness | ⬜ TODO | |

---

## Iteration 0: Infrastructure (DONE ✅)

### Issues Fixed (from Section 0 suspicions)
- ✅ a) Root Makefile expects build/user/*.elf but user/Makefile outputs user/*.elf
- ✅ b) No header dependency tracking (-MMD -MP) - Added to CFLAGS
- ✅ c) make hdd uses sudo losetup (forbidden) - Using mkfs.fat + mtools (no sudo)
- ✅ d) fat32_init reads LBA 0 as BPB but image is MBR-partitioned - Fixed to parse MBR
- ✅ e) -M q35 has no legacy IDE, ATA PIO may not see disk - Changed to -M pc
- ✅ f) fat32.c assumes 512-byte sectors - Now uses fs->bytes_per_sector everywhere

### Additional Fixes
- ✅ Linker PHDRs: Added user_programs PT_LOAD with proper page alignment
- ✅ Limine auto-download in Makefile
- ✅ User ELFs copied from user/ to build/user/ in root Makefile
- ✅ Test harness: scripts/test.sh with smoke/unit/persist/all suites
- ✅ Header dependency tracking (-MMD -MP) enabled
- ✅ QEMU machine changed from q35 to pc
- ✅ fat32.c updated to use fs->bytes_per_sector, added partition_lba
- ✅ Linker script: Added user_programs PT_LOAD with proper page alignment

### Iteration Log

#### Iteration 0.1 - Analyzed codebase
- **Change**: Read Makefiles, fat32.c, fat32.h, user/Makefile
- **Build**: Not tested yet
- **Next**: Fix Makefile issues and create test harness

#### Iteration 0.2 - Fixed Makefile & infrastructure
- **Change**: Added -MMD -MP, limine auto-download, user ELF copy, hdd-nosudo
- **Build**: Build passes
- **Next**: Fix fat32 MBR parsing and QEMU machine

#### Iteration 0.3 - Fixed fat32 & QEMU
- **Change**: Added partition_lba to fat32_fs_t, MBR parsing in fat32_init
- **Build**: Build passes
- **Next**: Fix linker PHDRs for Limine

#### Iteration 0.4 - Fixed linker PHDRs
- **Change**: Added user_programs PT_LOAD with proper page alignment in linker.ld
- **Build**: Build passes, boots to shell
- **Test**: ✅ SMOKE TEST PASSED

### Iteration 0 Summary
- **All infrastructure issues fixed**: a, b, c, d, e, f
- **Clean build**: make clean && make → 0 errors, only pre-existing warnings
- **Smoke test**: ✅ PASSES (boots to shell via ISO)
- **Ready for Sprint A Milestone A1**