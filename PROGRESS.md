# PROGRESS.md — ArchForge OS Sprint A Progress

## Sprint A: FAT32 Write/Persistence

### Milestone Status

| Milestone | Status | Notes |
|-----------|--------|-------|
| A1 MBR + partition offset | ✅ DONE | MBR parsing, partition LBA offset, multi-drive scan |
| A2 Block cache + FAT cache | ✅ DONE | 8-entry LRU block cache, full FAT cache, hit/miss counters |
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

---

## Sprint A Milestone A1: MBR + Partition Offset (DONE ✅)

### Implementation Summary
**Files Modified:**
- `include/fat32.h` - Added `partition_lba` and `drive` fields to `fat32_fs_t`
- `kernel/fat32.c` - MBR parsing in `fat32_init()`, drive-aware `read_sector`/`write_sector`
- `include/ata.h` - Updated API with drive parameter (0-3)
- `kernel/ata.c` - 4-drive support (Primary Master/Slave, Secondary Master/Slave)
- `kernel/kernel.c` - Scans drives 0-3 for FAT32 partitions

**Test Image Created:**
- 16MB MBR-partitioned FAT32 image (`build/fat32_test.img`)
- Partition at 2048 sector offset (1 MiB)
- Populated with `README.md` and `Makefile` via mtools

**Verification:**
```
[FAT32] Initialized on drive 0 (partition LBA: 0x800)
```

**Acceptance Test:**
✅ Smoke test passes (boots to shell via ISO with clean build)

---

## Sprint A Milestone A2: Block Cache + FAT Cache (DONE ✅)

### Implementation Summary

**Files Modified:**
- `include/fat32.h` - Added block cache struct (8 entries, LRU), FAT cache buffer, hit/miss counters
- `kernel/fat32.c` - Full cache implementation

**Block Cache (Write-Through):**
- 8-entry LRU cache with per-entry valid/dirty/last_access tracking
- Dynamic sector size via `fs->bytes_per_sector` (no hardcoded 512)
- `fat32_cache_read_sector()` / `fat32_cache_write_sector()` APIs
- Write-through policy: writes go to disk immediately, cache updated
- `fat32_cache_flush()` for explicit flush
- Hit/miss counters for monitoring

**FAT Cache:**
- Full FAT table cached in heap at init (`fat32_load_fat_cache()`)
- Caches all `num_fats` copies
- `fat32_get_fat_entry()` - O(1) cluster chain traversal
- `fat32_set_fat_entry()` - Write-through to all FAT copies
- `get_next_cluster()` now uses cache when available

**API Added:**
- `fat32_cache_read_sector()` - Read through block cache
- `fat32_cache_write_sector()` - Write through block cache
- `fat32_cache_flush()` - Flush dirty entries
- `fat32_cache_stats()` - Get hit/miss counters
- `fat32_load_fat_cache()` - Load FAT into heap
- `fat32_get_fat_entry()` - Get FAT entry from cache
- `fat32_set_fat_entry()` - Set FAT entry (write-through)

**Modified Read Paths:**
- `fat32_read_file()` - Uses `fat32_cache_read_sector()`
- `fat32_read_dir()` - Uses `fat32_cache_read_sector()`
- `get_next_cluster()` - Uses FAT cache when available

**Verification:**
- Clean build: `make clean && make` → 0 errors, only pre-existing warnings
- Smoke test: ✅ PASSES
- No hardcoded 512-byte assumptions - all use `fs->bytes_per_sector`

### Iteration A2 Log
- **A2.1**: Added block cache struct to fat32_fs_t (8 entries, LRU)
- **A2.2**: Implemented FAT cache loading at init
- **A2.3**: Write-through block cache with LRU eviction
- **A2.4**: Integrated cache into read_file/read_dir/get_next_cluster
- **A2.5**: Added cache statistics API
- **A2.6**: Smoke test green, build clean

### Next: Milestone A3 - Cluster Allocator