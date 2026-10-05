/* kernel/fat32.c — FAT32 Filesystem Driver
 * 
 * Full FAT32 driver with read/write support for ArchForge OS.
 * Implements A1-A7: MBR parsing, block cache, FAT cache,
 * cluster allocation, chain operations, file write,
 * directory entry creation, and filesystem operations.
 */
#include "fat32.h"
#include "ata.h"
#include "heap.h"
#include "serial.h"
#include "../include/string.h"

/* Simple LRU timestamp counter */
static uint64_t fat32_lru_counter = 0;

/* ====================================================================
 * Internal helper functions
 * ==================================================================== */

/* Helper to read a sector (with partition offset) */
static int read_sector(fat32_fs_t *fs, uint32_t lba, void *buffer) {
    return ata_read_sector(fs->drive, lba + fs->partition_lba, (uint8_t *)buffer, 1);
}

/* Helper to write a sector (with partition offset) */
static int write_sector(fat32_fs_t *fs, uint32_t lba, const void *buffer) {
    return ata_write_sector(fs->drive, lba + fs->partition_lba, (const uint8_t *)buffer, 1);
}

/* Helper to get cluster from directory entry */
static uint32_t get_cluster(fat32_dir_entry_t *entry) {
    return ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
}

/* Helper to set cluster in directory entry */
static void set_cluster(fat32_dir_entry_t *entry, uint32_t cluster) {
    entry->cluster_low = cluster & 0xFFFF;
    entry->cluster_high = (cluster >> 16) & 0xFFFF;
}

/* Helper to get next cluster from FAT (uses cache if available) */
static uint32_t get_next_cluster(fat32_fs_t *fs, uint32_t cluster) {
    if (fs->fat_buffer) {
        return fat32_get_fat_entry(fs, cluster);
    }
    
    /* Fallback to disk read */
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
    uint32_t fat_entry_offset = fat_offset % fs->bytes_per_sector;
    
    uint8_t sector[512];
    if (read_sector(fs, fat_sector, sector) != 0) {
        return FAT32_EOC;
    }
    
    uint32_t next_cluster = *(uint32_t *)(sector + fat_entry_offset);
    return next_cluster & 0x0FFFFFFF;
}

/* Check if cluster is end-of-chain */
static int is_eoc(uint32_t cluster) {
    return cluster >= FAT32_EOC && cluster <= FAT32_EOC_MAX;
}

/* Convert cluster number to first data sector */
static uint32_t cluster_to_sector(fat32_fs_t *fs, uint32_t cluster) {
    return fs->first_data_sector + (cluster - 2) * fs->sectors_per_cluster;
}

/* Helper to find or allocate a cache slot */
static int fat32_find_cache_slot(fat32_fs_t *fs, uint32_t lba) {
    int empty_slot = -1;
    uint64_t oldest = UINT64_MAX;
    int oldest_slot = 0;
    
    for (int i = 0; i < 8; i++) {
        if (!fs->block_cache[i].valid) {
            empty_slot = i;
            break;
        }
        if (fs->block_cache[i].sector_lba == lba) {
            return i;
        }
        if (fs->block_cache[i].last_access < oldest) {
            oldest = fs->block_cache[i].last_access;
            oldest_slot = i;
        }
    }
    
    return (empty_slot >= 0) ? empty_slot : oldest_slot;
}

/* ====================================================================
 * Cache management functions
 * ==================================================================== */

int fat32_cache_read_sector(fat32_fs_t *fs, uint32_t lba, void *buffer) {
    fat32_lru_counter++;
    
    int slot = fat32_find_cache_slot(fs, lba);
    if (slot < 0) return -1;
    
    if (fs->block_cache[slot].valid && fs->block_cache[slot].sector_lba == lba) {
        fs->cache_hits++;
        fs->block_cache[slot].last_access = fat32_lru_counter;
        memcpy(buffer, fs->block_cache[slot].data, fs->bytes_per_sector);
        return 0;
    }
    
    fs->cache_misses++;
    
    if (!fs->block_cache[slot].data) {
        fs->block_cache[slot].data = kmalloc(fs->bytes_per_sector);
        if (!fs->block_cache[slot].data) return -1;
    }
    
    if (read_sector(fs, lba, fs->block_cache[slot].data) != 0) {
        return -1;
    }
    
    fs->block_cache[slot].sector_lba = lba;
    fs->block_cache[slot].valid = 1;
    fs->block_cache[slot].dirty = 0;
    fs->block_cache[slot].last_access = fat32_lru_counter;
    
    memcpy(buffer, fs->block_cache[slot].data, fs->bytes_per_sector);
    return 0;
}

int fat32_cache_write_sector(fat32_fs_t *fs, uint32_t lba, const void *buffer) {
    fat32_lru_counter++;
    
    int slot = fat32_find_cache_slot(fs, lba);
    if (slot < 0) return -1;
    
    if (!fs->block_cache[slot].data) {
        fs->block_cache[slot].data = kmalloc(fs->bytes_per_sector);
        if (!fs->block_cache[slot].data) return -1;
    }
    
    /* Write-through: write to disk immediately */
    if (write_sector(fs, lba, buffer) != 0) {
        return -1;
    }
    
    memcpy(fs->block_cache[slot].data, buffer, fs->bytes_per_sector);
    fs->block_cache[slot].sector_lba = lba;
    fs->block_cache[slot].valid = 1;
    fs->block_cache[slot].dirty = 0;
    fs->block_cache[slot].last_access = fat32_lru_counter;
    
    return 0;
}

void fat32_cache_flush(fat32_fs_t *fs) {
    for (int i = 0; i < 8; i++) {
        if (fs->block_cache[i].valid && fs->block_cache[i].dirty) {
            write_sector(fs, fs->block_cache[i].sector_lba, fs->block_cache[i].data);
            fs->block_cache[i].dirty = 0;
        }
    }
}

void fat32_cache_stats(fat32_fs_t *fs, uint32_t *hits, uint32_t *misses) {
    if (hits) *hits = fs->cache_hits;
    if (misses) *misses = fs->cache_misses;
}

int fat32_load_fat_cache(fat32_fs_t *fs) {
    size_t fat_size = fs->fat_size_32 * fs->bytes_per_sector;
    fs->fat_buffer = kmalloc(fat_size * fs->num_fats);
    if (!fs->fat_buffer) return -1;
    
    for (uint8_t fat = 0; fat < fs->num_fats; fat++) {
        uint32_t fat_start = fs->fat_start_sector + fat * fs->fat_size_32;
        for (uint32_t i = 0; i < fs->fat_size_32; i++) {
            if (read_sector(fs, fat_start + i, fs->fat_buffer + (fat * fat_size) + i * fs->bytes_per_sector) != 0) {
                kfree(fs->fat_buffer);
                fs->fat_buffer = NULL;
                return -1;
            }
        }
    }
    return 0;
}

uint32_t fat32_get_fat_entry(fat32_fs_t *fs, uint32_t cluster) {
    if (!fs->fat_buffer) return FAT32_EOC;
    
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_offset / fs->bytes_per_sector;
    uint32_t fat_entry_offset = fat_offset % fs->bytes_per_sector;
    
    uint8_t *fat_data = fs->fat_buffer;
    return *(uint32_t *)(fat_data + fat_sector * fs->bytes_per_sector + fat_entry_offset) & 0x0FFFFFFF;
}

int fat32_set_fat_entry(fat32_fs_t *fs, uint32_t cluster, uint32_t value) {
    if (!fs->fat_buffer) return -1;
    
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_offset / fs->bytes_per_sector;
    uint32_t fat_entry_offset = fat_offset % fs->bytes_per_sector;
    
    value &= 0x0FFFFFFF;
    
    for (uint8_t fat = 0; fat < fs->num_fats; fat++) {
        uint32_t fat_start = fs->fat_start_sector + fat * fs->fat_size_32;
        uint8_t sector_buf[512];
        
        if (read_sector(fs, fat_start + fat_sector, sector_buf) != 0) return -1;
        
        *(uint32_t *)(sector_buf + fat_entry_offset) = value;
        
        if (write_sector(fs, fat_start + fat_sector, sector_buf) != 0) return -1;
    }
    
    /* Update cache */
    uint8_t *fat_data = fs->fat_buffer;
    *(uint32_t *)(fat_data + fat_sector * fs->bytes_per_sector + fat_entry_offset) = value;
    
    return 0;
}

void fat32_format_name(const uint8_t *fat_name, char *out) {
    int i, j = 0;
    for (i = 0; i < 8; i++) {
        if (fat_name[i] == ' ') break;
        out[j++] = fat_name[i];
    }
    if (fat_name[8] != ' ') {
        out[j++] = '.';
        for (i = 8; i < 11; i++) {
            if (fat_name[i] == ' ') break;
            out[j++] = fat_name[i];
        }
    }
    out[j] = '\0';
    
    for (i = 0; i < j; i++) {
        if (out[i] >= 'A' && out[i] <= 'Z') {
            out[i] = out[i] + 32;
        }
    }
}

/* ====================================================================
 * Initialization
 * ==================================================================== */

int fat32_init(int drive, fat32_fs_t *fs) {
    uint8_t mbr[512];
    if (ata_read_sector(drive, 0, mbr, 1) != 0) {
        return -1;
    }
    
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        return -1;
    }
    
    uint32_t partition_lba = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t *entry = mbr + 0x1BE + i * 16;
        uint8_t type = entry[4];
        if (type == 0x0B || type == 0x0C || type == 0x1B || type == 0x1C) {
            partition_lba = *(uint32_t *)(entry + 8);
            break;
        }
    }
    
    if (partition_lba == 0) {
        /* Try superfloppy (no partition table) */
        partition_lba = 0;
        /* Check if LBA 0 is a valid BPB */
        fat32_bpb_t *bpb_check = (fat32_bpb_t *)mbr;
        if (bpb_check->bytes_per_sector == 0 || bpb_check->bytes_per_sector % 512 != 0) {
            return -1;
        }
        if (strncmp((char *)bpb_check->fs_type, "FAT32", 5) != 0) {
            return -1;
        }
    }
    
    fs->partition_lba = partition_lba;
    fs->drive = drive;
    
    /* Initialize cache */
    for (int i = 0; i < 8; i++) {
        fs->block_cache[i].valid = 0;
        fs->block_cache[i].dirty = 0;
        fs->block_cache[i].data = NULL;
        fs->block_cache[i].last_access = 0;
        fs->block_cache[i].sector_lba = 0;
    }
    fs->cache_hits = 0;
    fs->cache_misses = 0;
    fs->fat_buffer = NULL;
    fs->next_free_cluster = 2;
    fs->free_cluster_count = 0;
    
    /* Read the BPB from the partition start */
    fat32_bpb_t bpb;
    if (read_sector(fs, 0, &bpb) != 0) {
        return -1;
    }
    
    /* Validate BPB */
    if (bpb.boot_signature != 0x29 && bpb.boot_signature != 0x28) {
        if (strncmp((char *)bpb.fs_type, "FAT32", 5) != 0) {
            return -1;
        }
    }
    
    fs->bytes_per_sector = bpb.bytes_per_sector;
    fs->sectors_per_cluster = bpb.sectors_per_cluster;
    fs->root_cluster = bpb.root_cluster;
    fs->num_fats = bpb.num_fats;
    fs->fat_size_32 = bpb.fat_size_32;
    
    fs->fat_start_sector = bpb.reserved_sector_count;
    uint32_t fat_end_sector = fs->fat_start_sector + (fs->num_fats * fs->fat_size_32);
    fs->first_data_sector = fat_end_sector;
    
    /* Calculate total clusters */
    uint32_t total_sectors = bpb.total_sectors_32;
    if (total_sectors == 0) total_sectors = bpb.total_sectors_16;
    uint32_t data_sectors = total_sectors - fs->first_data_sector;
    fs->total_clusters = data_sectors / fs->sectors_per_cluster;
    
    /* Load FAT into cache */
    if (fat32_load_fat_cache(fs) != 0) {
        serial_write("[FAT32] WARNING: Failed to load FAT cache\n");
    }
    
    /* Read FSInfo sector if available */
    if (bpb.fs_info > 0 && bpb.fs_info < fs->fat_start_sector) {
        uint8_t fsinfo_buf[512];
        if (read_sector(fs, bpb.fs_info, fsinfo_buf) == 0) {
            /* FSInfo structure: lead signature at 0, struct signature at 484, trail signature at 508 */
            uint32_t lead_sig = *(uint32_t *)(fsinfo_buf + 0);
            uint32_t struct_sig = *(uint32_t *)(fsinfo_buf + 484);
            if (lead_sig == 0x41615252 && struct_sig == 0x61417272) {
                uint32_t free_count = *(uint32_t *)(fsinfo_buf + 488);
                uint32_t next_free = *(uint32_t *)(fsinfo_buf + 492);
                if (free_count != 0xFFFFFFFF) {
                    fs->free_cluster_count = free_count;
                }
                if (next_free != 0xFFFFFFFF && next_free >= 2) {
                    fs->next_free_cluster = next_free;
                }
            }
        }
    }
    
    serial_write("[FAT32] Initialized on drive ");
    char drive_str[4];
    drive_str[0] = '0' + drive;
    drive_str[1] = '\0';
    serial_write(drive_str);
    serial_write(" (partition LBA: ");
    serial_write_hex(partition_lba);
    serial_write(", clusters: ");
    serial_write_dec(fs->total_clusters);
    serial_write(")\n");
    
    return 0;
}

/* ====================================================================
 * Read operations
 * ==================================================================== */

int fat32_read_file(fat32_fs_t *fs, uint32_t cluster, void *buffer, size_t size, size_t offset) {
    if (cluster < 2 || is_eoc(cluster)) return -1;
    
    uint8_t *buf = (uint8_t *)buffer;
    size_t bytes_read = 0;
    size_t current_offset = 0;
    
    while (cluster >= 2 && !is_eoc(cluster)) {
        uint32_t sector = cluster_to_sector(fs, cluster);
        
        for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) {
            uint8_t sec_buf[512];
            if (fat32_cache_read_sector(fs, sector + i, sec_buf) != 0) {
                return -1;
            }
            
            size_t sector_size = fs->bytes_per_sector;
            if (current_offset + sector_size > offset + size) {
                sector_size = (offset + size) - current_offset;
            }
            
            if (current_offset >= offset) {
                size_t copy_size = sector_size;
                if (current_offset + copy_size > offset + size) {
                    copy_size = (offset + size) - current_offset;
                }
                size_t src_offset = 0;
                if (current_offset < offset) {
                    src_offset = offset - current_offset;
                    copy_size = sector_size - src_offset;
                }
                
                memcpy(buf + bytes_read, sec_buf + src_offset, copy_size);
                bytes_read += copy_size;
            }
            
            current_offset += sector_size;
            if (current_offset >= offset + size) {
                return bytes_read;
            }
        }
        
        cluster = get_next_cluster(fs, cluster);
    }
    
    return bytes_read;
}

int fat32_read_dir(fat32_fs_t *fs, uint32_t cluster, void *buffer, size_t max_entries) {
    if (cluster < 2 || is_eoc(cluster)) return -1;
    
    fat32_dir_entry_t *entries = (fat32_dir_entry_t *)buffer;
    size_t entry_count = 0;
    
    while (cluster >= 2 && !is_eoc(cluster)) {
        uint32_t sector = cluster_to_sector(fs, cluster);
        
        for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) {
            uint8_t sec_buf[512];
            if (fat32_cache_read_sector(fs, sector + i, sec_buf) != 0) {
                return -1;
            }
            
            for (size_t j = 0; j < 512 / sizeof(fat32_dir_entry_t); j++) {
                fat32_dir_entry_t *entry = (fat32_dir_entry_t *)(sec_buf + j * sizeof(fat32_dir_entry_t));
                
                if (entry->name[0] == 0x00) {
                    return entry_count;
                }
                
                if (entry->name[0] == 0xE5) {
                    continue;
                }
                
                if (entry->attr == 0x08 || entry->attr == 0x0F) {
                    continue;
                }
                
                if (entry_count < max_entries) {
                    entries[entry_count++] = *entry;
                }
            }
        }
        
        cluster = get_next_cluster(fs, cluster);
    }
    
    return entry_count;
}

/* ====================================================================
 * A3: Cluster allocator
 * ==================================================================== */

uint32_t fat32_count_free_clusters(fat32_fs_t *fs) {
    if (!fs->fat_buffer) return 0;
    
    uint32_t free_count = 0;
    for (uint32_t cluster = 2; cluster < fs->total_clusters + 2; cluster++) {
        if (fat32_get_fat_entry(fs, cluster) == FAT32_FREE) {
            free_count++;
        }
    }
    
    return free_count;
}

uint32_t fat32_alloc_cluster(fat32_fs_t *fs) {
    if (!fs->fat_buffer) return 0;
    
    /* Start searching from the hint (next_free_cluster) */
    uint32_t start = fs->next_free_cluster;
    if (start < 2) start = 2;
    uint32_t max_cluster = fs->total_clusters + 2;
    
    /* Search from start to end */
    for (uint32_t cluster = start; cluster < max_cluster; cluster++) {
        if (fat32_get_fat_entry(fs, cluster) == FAT32_FREE) {
            if (fat32_set_fat_entry(fs, cluster, FAT32_EOC) != 0) {
                return 0;
            }
            fs->next_free_cluster = cluster + 1;
            if (fs->free_cluster_count > 0) fs->free_cluster_count--;
            return cluster;
        }
    }
    
    /* Wrap around: search from 2 to start */
    for (uint32_t cluster = 2; cluster < start; cluster++) {
        if (fat32_get_fat_entry(fs, cluster) == FAT32_FREE) {
            if (fat32_set_fat_entry(fs, cluster, FAT32_EOC) != 0) {
                return 0;
            }
            fs->next_free_cluster = cluster + 1;
            if (fs->free_cluster_count > 0) fs->free_cluster_count--;
            return cluster;
        }
    }
    
    serial_write("[FAT32] No free clusters available!\n");
    return 0;
}

int fat32_free_cluster(fat32_fs_t *fs, uint32_t cluster) {
    if (!fs->fat_buffer) return -1;
    if (cluster < 2) return -1;
    if (cluster >= fs->total_clusters + 2) return -1;
    
    if (fat32_set_fat_entry(fs, cluster, FAT32_FREE) != 0) {
        return -1;
    }
    
    fs->free_cluster_count++;
    return 0;
}

int fat32_free_chain(fat32_fs_t *fs, uint32_t start_cluster) {
    if (!fs->fat_buffer) return -1;
    if (start_cluster < 2) return -1;
    
    uint32_t cluster = start_cluster;
    while (cluster >= 2 && !is_eoc(cluster)) {
        uint32_t next = fat32_get_fat_entry(fs, cluster);
        
        if (fat32_set_fat_entry(fs, cluster, FAT32_FREE) != 0) {
            return -1;
        }
        fs->free_cluster_count++;
        
        cluster = next;
    }
    
    return 0;
}

/* ====================================================================
 * A4: Chain operations
 * ==================================================================== */

uint32_t fat32_chain_length(fat32_fs_t *fs, uint32_t start_cluster) {
    if (start_cluster < 2 || is_eoc(start_cluster)) return 0;
    
    uint32_t count = 0;
    uint32_t cluster = start_cluster;
    
    while (cluster >= 2 && !is_eoc(cluster)) {
        count++;
        cluster = get_next_cluster(fs, cluster);
        if (count > fs->total_clusters) break; /* Safety: prevent infinite loop */
    }
    
    return count;
}

uint32_t fat32_chain_last(fat32_fs_t *fs, uint32_t start_cluster) {
    if (start_cluster < 2 || is_eoc(start_cluster)) return start_cluster;
    
    uint32_t cluster = start_cluster;
    uint32_t next;
    
    while (1) {
        next = get_next_cluster(fs, cluster);
        if (next < 2 || is_eoc(next)) {
            return cluster;
        }
        cluster = next;
    }
}

uint32_t fat32_extend_chain(fat32_fs_t *fs, uint32_t last_cluster) {
    if (last_cluster < 2) return 0;
    
    /* Allocate a new cluster */
    uint32_t new_cluster = fat32_alloc_cluster(fs);
    if (new_cluster == 0) return 0;
    
    /* Link the last cluster to the new one */
    if (fat32_set_fat_entry(fs, last_cluster, new_cluster) != 0) {
        fat32_free_cluster(fs, new_cluster);
        return 0;
    }
    
    return new_cluster;
}

int fat32_truncate_chain(fat32_fs_t *fs, uint32_t start_cluster, uint32_t keep_count) {
    if (start_cluster < 2) return -1;
    if (keep_count == 0) {
        /* Free entire chain */
        return fat32_free_chain(fs, start_cluster);
    }
    
    uint32_t cluster = start_cluster;
    uint32_t count = 1;
    
    /* Walk to the keep_count-th cluster */
    while (count < keep_count) {
        uint32_t next = get_next_cluster(fs, cluster);
        if (next < 2 || is_eoc(next)) {
            /* Chain is shorter than keep_count, nothing to truncate */
            return 0;
        }
        cluster = next;
        count++;
    }
    
    /* cluster is now the last cluster to keep */
    /* Get the next cluster (first one to free) */
    uint32_t first_free = get_next_cluster(fs, cluster);
    
    /* Mark current cluster as EOC */
    if (fat32_set_fat_entry(fs, cluster, FAT32_EOC) != 0) {
        return -1;
    }
    
    /* Free the rest of the chain */
    if (first_free >= 2 && !is_eoc(first_free)) {
        return fat32_free_chain(fs, first_free);
    }
    
    return 0;
}

uint32_t fat32_ensure_chain(fat32_fs_t *fs, uint32_t start_cluster, uint32_t needed) {
    if (needed == 0) return start_cluster;
    
    if (start_cluster == 0) {
        /* Need to allocate the first cluster */
        start_cluster = fat32_alloc_cluster(fs);
        if (start_cluster == 0) return 0;
    }
    
    uint32_t current_length = fat32_chain_length(fs, start_cluster);
    if (current_length >= needed) {
        return start_cluster;
    }
    
    /* Need to add more clusters */
    uint32_t last = fat32_chain_last(fs, start_cluster);
    uint32_t to_add = needed - current_length;
    
    for (uint32_t i = 0; i < to_add; i++) {
        last = fat32_extend_chain(fs, last);
        if (last == 0) return 0; /* Allocation failed */
    }
    
    return start_cluster;
}

/* ====================================================================
 * A5: File write operations
 * ==================================================================== */

int fat32_write_file(fat32_fs_t *fs, uint32_t start_cluster,
                     const void *data, size_t size, uint32_t *new_start) {
    if (!data || size == 0) {
        if (new_start) *new_start = start_cluster;
        return 0;
    }
    
    uint32_t bytes_per_cluster = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint32_t clusters_needed = (size + bytes_per_cluster - 1) / bytes_per_cluster;
    
    /* Ensure we have enough clusters */
    start_cluster = fat32_ensure_chain(fs, start_cluster, clusters_needed);
    if (start_cluster == 0) {
        serial_write("[FAT32] Failed to allocate clusters for write\n");
        return -1;
    }
    
    if (new_start) *new_start = start_cluster;
    
    /* Write data cluster by cluster */
    const uint8_t *src = (const uint8_t *)data;
    size_t remaining = size;
    uint32_t cluster = start_cluster;
    
    while (remaining > 0 && cluster >= 2 && !is_eoc(cluster)) {
        uint32_t sector = cluster_to_sector(fs, cluster);
        
        for (uint32_t i = 0; i < fs->sectors_per_cluster && remaining > 0; i++) {
            uint8_t sec_buf[512];
            size_t to_write = fs->bytes_per_sector;
            if (to_write > remaining) to_write = remaining;
            
            if (to_write < fs->bytes_per_sector) {
                /* Partial sector: read-modify-write */
                if (fat32_cache_read_sector(fs, sector + i, sec_buf) != 0) {
                    memset(sec_buf, 0, fs->bytes_per_sector);
                }
                memcpy(sec_buf, src, to_write);
            } else {
                /* Full sector: write directly */
                memcpy(sec_buf, src, fs->bytes_per_sector);
            }
            
            if (fat32_cache_write_sector(fs, sector + i, sec_buf) != 0) {
                return -1;
            }
            
            src += to_write;
            remaining -= to_write;
        }
        
        cluster = get_next_cluster(fs, cluster);
    }
    
    return (int)size;
}

/* ====================================================================
 * A6: Directory entry operations
 * ==================================================================== */

void fat32_make_short_name(const char *name, uint8_t *short_name) {
    /* Initialize with spaces */
    memset(short_name, ' ', 11);
    
    /* Find the dot */
    const char *dot = NULL;
    const char *p = name;
    while (*p) {
        if (*p == '.') dot = p;
        p++;
    }
    
    /* Copy name part (up to 8 chars) */
    int name_len = dot ? (dot - name) : (p - name);
    if (name_len > 8) name_len = 8;
    for (int i = 0; i < name_len; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32; /* Uppercase */
        short_name[i] = c;
    }
    
    /* Copy extension part (up to 3 chars) */
    if (dot) {
        dot++; /* Skip the dot */
        int ext_len = 0;
        while (dot[ext_len] && ext_len < 3) {
            char c = dot[ext_len];
            if (c >= 'a' && c <= 'z') c -= 32;
            short_name[8 + ext_len] = c;
            ext_len++;
        }
    }
}

int fat32_find_entry(fat32_fs_t *fs, uint32_t dir_cluster, const char *name,
                     fat32_dir_entry_t *entry_out,
                     uint32_t *dir_cluster_out, int *entry_index_out) {
    if (!name || !name[0]) return -1;
    
    uint8_t target_name[11];
    fat32_make_short_name(name, target_name);
    
    uint32_t cluster = dir_cluster;
    
    while (cluster >= 2 && !is_eoc(cluster)) {
        uint32_t sector = cluster_to_sector(fs, cluster);
        
        for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) {
            uint8_t sec_buf[512];
            if (fat32_cache_read_sector(fs, sector + i, sec_buf) != 0) {
                return -1;
            }
            
            int entries_per_sector = fs->bytes_per_sector / sizeof(fat32_dir_entry_t);
            for (int j = 0; j < entries_per_sector; j++) {
                fat32_dir_entry_t *entry = (fat32_dir_entry_t *)(sec_buf + j * sizeof(fat32_dir_entry_t));
                
                if (entry->name[0] == 0x00) {
                    return -1; /* End of directory */
                }
                
                if (entry->name[0] == 0xE5) {
                    continue; /* Deleted */
                }
                
                if (entry->attr == 0x0F) {
                    continue; /* LFN entry */
                }
                
                /* Compare names */
                if (memcmp(entry->name, target_name, 11) == 0) {
                    if (entry_out) *entry_out = *entry;
                    if (dir_cluster_out) *dir_cluster_out = cluster;
                    if (entry_index_out) *entry_index_out = (i * entries_per_sector) + j;
                    return 0;
                }
            }
        }
        
        cluster = get_next_cluster(fs, cluster);
    }
    
    return -1; /* Not found */
}

int fat32_create_file(fat32_fs_t *fs, uint32_t parent_dir, const char *name,
                      uint32_t start_cluster, uint32_t size, uint8_t attr) {
    /* Create the directory entry */
    fat32_dir_entry_t new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    fat32_make_short_name(name, new_entry.name);
    new_entry.attr = attr;
    set_cluster(&new_entry, start_cluster);
    new_entry.file_size = size;
    
    /* Find an empty slot in the directory */
    uint32_t cluster = parent_dir;
    
    while (cluster >= 2 && !is_eoc(cluster)) {
        uint32_t sector = cluster_to_sector(fs, cluster);
        
        for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) {
            uint8_t sec_buf[512];
            if (fat32_cache_read_sector(fs, sector + i, sec_buf) != 0) {
                return -1;
            }
            
            int entries_per_sector = fs->bytes_per_sector / sizeof(fat32_dir_entry_t);
            for (int j = 0; j < entries_per_sector; j++) {
                fat32_dir_entry_t *entry = (fat32_dir_entry_t *)(sec_buf + j * sizeof(fat32_dir_entry_t));
                
                /* Empty slot or deleted slot */
                if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {
                    /* Write the new entry */
                    memcpy(entry, &new_entry, sizeof(fat32_dir_entry_t));
                    
                    if (fat32_cache_write_sector(fs, sector + i, sec_buf) != 0) {
                        return -1;
                    }
                    
                    return 0;
                }
            }
        }
        
        cluster = get_next_cluster(fs, cluster);
    }
    
    /* Directory is full - need to extend it */
    uint32_t last_cluster = fat32_chain_last(fs, parent_dir);
    uint32_t new_cluster = fat32_extend_chain(fs, last_cluster);
    if (new_cluster == 0) {
        serial_write("[FAT32] Directory full and cannot extend\n");
        return -1;
    }
    
    /* Zero out the new cluster */
    uint32_t sector = cluster_to_sector(fs, new_cluster);
    uint8_t zero_buf[512];
    memset(zero_buf, 0, sizeof(zero_buf));
    for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) {
        if (fat32_cache_write_sector(fs, sector + i, zero_buf) != 0) {
            return -1;
        }
    }
    
    /* Now write the entry in the first slot of the new cluster */
    uint8_t sec_buf[512];
    memset(sec_buf, 0, sizeof(sec_buf));
    memcpy(sec_buf, &new_entry, sizeof(fat32_dir_entry_t));
    if (fat32_cache_write_sector(fs, sector, sec_buf) != 0) {
        return -1;
    }
    
    return 0;
}

int fat32_update_dir_entry(fat32_fs_t *fs, uint32_t dir_cluster,
                           int entry_index, uint32_t new_size,
                           uint32_t new_start_cluster) {
    int entries_per_sector = fs->bytes_per_sector / sizeof(fat32_dir_entry_t);
    
    /* Walk the chain to find the right cluster */
    uint32_t cluster = dir_cluster;
    int entries_skipped = 0;
    
    while (cluster >= 2 && !is_eoc(cluster)) {
        int entries_in_cluster = fs->sectors_per_cluster * entries_per_sector;
        
        if (entries_skipped + entries_in_cluster > entry_index) {
            /* Entry is in this cluster */
            int local_index = entry_index - entries_skipped;
            int sector_in_cluster = local_index / entries_per_sector;
            int entry_in_sector = local_index % entries_per_sector;
            
            uint32_t sector = cluster_to_sector(fs, cluster) + sector_in_cluster;
            uint8_t sec_buf[512];
            
            if (fat32_cache_read_sector(fs, sector, sec_buf) != 0) {
                return -1;
            }
            
            fat32_dir_entry_t *entry = (fat32_dir_entry_t *)(sec_buf + entry_in_sector * sizeof(fat32_dir_entry_t));
            entry->file_size = new_size;
            set_cluster(entry, new_start_cluster);
            
            if (fat32_cache_write_sector(fs, sector, sec_buf) != 0) {
                return -1;
            }
            
            return 0;
        }
        
        entries_skipped += entries_in_cluster;
        cluster = get_next_cluster(fs, cluster);
    }
    
    return -1; /* Entry not found */
}

/* ====================================================================
 * A7: Filesystem operations
 * ==================================================================== */

int fat32_mkdir(fat32_fs_t *fs, uint32_t parent_dir, const char *name) {
    /* Allocate a cluster for the new directory */
    uint32_t new_cluster = fat32_alloc_cluster(fs);
    if (new_cluster == 0) {
        serial_write("[FAT32] Cannot allocate cluster for directory\n");
        return -1;
    }
    
    /* Create the . and .. entries */
    uint32_t sector = cluster_to_sector(fs, new_cluster);
    uint8_t zero_buf[512];
    memset(zero_buf, 0, sizeof(zero_buf));
    
    /* Zero out all sectors in the cluster */
    for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) {
        if (fat32_cache_write_sector(fs, sector + i, zero_buf) != 0) {
            fat32_free_cluster(fs, new_cluster);
            return -1;
        }
    }
    
    /* Write . entry */
    fat32_dir_entry_t dot_entry;
    memset(&dot_entry, 0, sizeof(dot_entry));
    memset(dot_entry.name, ' ', 11);
    dot_entry.name[0] = '.';
    dot_entry.attr = FAT32_ATTR_DIRECTORY;
    set_cluster(&dot_entry, new_cluster);
    
    /* Write .. entry */
    fat32_dir_entry_t dotdot_entry;
    memset(&dotdot_entry, 0, sizeof(dotdot_entry));
    memset(dotdot_entry.name, ' ', 11);
    dotdot_entry.name[0] = '.';
    dotdot_entry.name[1] = '.';
    dotdot_entry.attr = FAT32_ATTR_DIRECTORY;
    set_cluster(&dotdot_entry, parent_dir);
    
    /* Write the first sector with . and .. */
    uint8_t first_sector[512];
    memset(first_sector, 0, sizeof(first_sector));
    memcpy(first_sector, &dot_entry, sizeof(fat32_dir_entry_t));
    memcpy(first_sector + sizeof(fat32_dir_entry_t), &dotdot_entry, sizeof(fat32_dir_entry_t));
    
    if (fat32_cache_write_sector(fs, sector, first_sector) != 0) {
        fat32_free_cluster(fs, new_cluster);
        return -1;
    }
    
    /* Create the directory entry in the parent */
    if (fat32_create_file(fs, parent_dir, name, new_cluster, 0, FAT32_ATTR_DIRECTORY) != 0) {
        fat32_free_cluster(fs, new_cluster);
        return -1;
    }
    
    return 0;
}

int fat32_unlink(fat32_fs_t *fs, uint32_t dir_cluster, const char *name) {
    fat32_dir_entry_t entry;
    uint32_t found_cluster;
    int entry_index;
    
    if (fat32_find_entry(fs, dir_cluster, name, &entry, &found_cluster, &entry_index) != 0) {
        return -1; /* Not found */
    }
    
    if (entry.attr & FAT32_ATTR_DIRECTORY) {
        return -1; /* Use rmdir for directories */
    }
    
    /* Free the file's cluster chain */
    uint32_t start_cluster = get_cluster(&entry);
    if (start_cluster >= 2) {
        if (fat32_free_chain(fs, start_cluster) != 0) {
            return -1;
        }
    }
    
    /* Mark the directory entry as deleted */
    int entries_per_sector = fs->bytes_per_sector / sizeof(fat32_dir_entry_t);
    uint32_t cluster = found_cluster;
    int sector_in_cluster = (entry_index % (fs->sectors_per_cluster * entries_per_sector)) / entries_per_sector;
    int entry_in_sector = entry_index % entries_per_sector;
    
    uint32_t sector = cluster_to_sector(fs, cluster) + sector_in_cluster;
    uint8_t sec_buf[512];
    
    if (fat32_cache_read_sector(fs, sector, sec_buf) != 0) {
        return -1;
    }
    
    fat32_dir_entry_t *del_entry = (fat32_dir_entry_t *)(sec_buf + entry_in_sector * sizeof(fat32_dir_entry_t));
    del_entry->name[0] = 0xE5; /* Mark as deleted */
    
    if (fat32_cache_write_sector(fs, sector, sec_buf) != 0) {
        return -1;
    }
    
    return 0;
}

int fat32_rmdir(fat32_fs_t *fs, uint32_t dir_cluster, const char *name) {
    fat32_dir_entry_t entry;
    uint32_t found_cluster;
    int entry_index;
    
    if (fat32_find_entry(fs, dir_cluster, name, &entry, &found_cluster, &entry_index) != 0) {
        return -1;
    }
    
    if (!(entry.attr & FAT32_ATTR_DIRECTORY)) {
        return -1; /* Not a directory */
    }
    
    uint32_t target_cluster = get_cluster(&entry);
    
    /* Check if directory is empty (only . and .. entries) */
    uint32_t cluster = target_cluster;
    int entry_count = 0;
    
    while (cluster >= 2 && !is_eoc(cluster)) {
        uint32_t sector = cluster_to_sector(fs, cluster);
        
        for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) {
            uint8_t sec_buf[512];
            if (fat32_cache_read_sector(fs, sector + i, sec_buf) != 0) {
                return -1;
            }
            
            int entries_per_sector = fs->bytes_per_sector / sizeof(fat32_dir_entry_t);
            for (int j = 0; j < entries_per_sector; j++) {
                fat32_dir_entry_t *dir_entry = (fat32_dir_entry_t *)(sec_buf + j * sizeof(fat32_dir_entry_t));
                
                if (dir_entry->name[0] == 0x00) {
                    goto check_done; /* End of directory */
                }
                
                if (dir_entry->name[0] == 0xE5) {
                    continue; /* Deleted */
                }
                
                if (dir_entry->attr == 0x0F) {
                    continue; /* LFN */
                }
                
                /* Check if it's . or .. */
                if (dir_entry->name[0] == '.' && 
                    (dir_entry->name[1] == ' ' || 
                     (dir_entry->name[1] == '.' && dir_entry->name[2] == ' '))) {
                    continue; /* Skip . and .. */
                }
                
                entry_count++;
            }
        }
        
        cluster = get_next_cluster(fs, cluster);
    }
    
check_done:
    if (entry_count > 0) {
        serial_write("[FAT32] Directory not empty\n");
        return -1; /* Directory not empty */
    }
    
    /* Free the directory's cluster chain */
    if (target_cluster >= 2) {
        if (fat32_free_chain(fs, target_cluster) != 0) {
            return -1;
        }
    }
    
    /* Mark the directory entry as deleted in parent */
    int entries_per_sector = fs->bytes_per_sector / sizeof(fat32_dir_entry_t);
    uint32_t parent_cluster = found_cluster;
    int sector_in_cluster = (entry_index % (fs->sectors_per_cluster * entries_per_sector)) / entries_per_sector;
    int entry_in_sector = entry_index % entries_per_sector;
    
    uint32_t sector = cluster_to_sector(fs, parent_cluster) + sector_in_cluster;
    uint8_t sec_buf[512];
    
    if (fat32_cache_read_sector(fs, sector, sec_buf) != 0) {
        return -1;
    }
    
    fat32_dir_entry_t *del_entry = (fat32_dir_entry_t *)(sec_buf + entry_in_sector * sizeof(fat32_dir_entry_t));
    del_entry->name[0] = 0xE5;
    
    if (fat32_cache_write_sector(fs, sector, sec_buf) != 0) {
        return -1;
    }
    
    return 0;
}

int fat32_rename(fat32_fs_t *fs, uint32_t dir_cluster,
                 const char *old_name, const char *new_name) {
    fat32_dir_entry_t entry;
    uint32_t found_cluster;
    int entry_index;
    
    /* Find the old entry */
    if (fat32_find_entry(fs, dir_cluster, old_name, &entry, &found_cluster, &entry_index) != 0) {
        return -1;
    }
    
    /* Check if new name already exists */
    fat32_dir_entry_t check_entry;
    if (fat32_find_entry(fs, dir_cluster, new_name, &check_entry, NULL, NULL) == 0) {
        return -1; /* New name already exists */
    }
    
    /* Update the name in the old entry */
    fat32_make_short_name(new_name, entry.name);
    
    /* Write the updated entry back */
    int entries_per_sector = fs->bytes_per_sector / sizeof(fat32_dir_entry_t);
    uint32_t cluster = found_cluster;
    int sector_in_cluster = (entry_index % (fs->sectors_per_cluster * entries_per_sector)) / entries_per_sector;
    int entry_in_sector = entry_index % entries_per_sector;
    
    uint32_t sector = cluster_to_sector(fs, cluster) + sector_in_cluster;
    uint8_t sec_buf[512];
    
    if (fat32_cache_read_sector(fs, sector, sec_buf) != 0) {
        return -1;
    }
    
    fat32_dir_entry_t *target = (fat32_dir_entry_t *)(sec_buf + entry_in_sector * sizeof(fat32_dir_entry_t));
    memcpy(target->name, entry.name, 11);
    
    if (fat32_cache_write_sector(fs, sector, sec_buf) != 0) {
        return -1;
    }
    
    return 0;
}

/* Resolve a full path to a directory entry.
 * Path format: "/dir1/dir2/file.txt" or "dir1/dir2/file.txt" */
int fat32_resolve_path(fat32_fs_t *fs, const char *path,
                       fat32_dir_entry_t *entry_out,
                       uint32_t *parent_dir_out) {
    if (!path) return -1;
    
    /* Start from root directory */
    uint32_t current_dir = fs->root_cluster;
    const char *p = path;
    
    /* Skip leading slashes */
    while (*p == '/') p++;
    
    if (!*p) {
        /* Root directory */
        if (entry_out) {
            memset(entry_out, 0, sizeof(*entry_out));
            entry_out->attr = FAT32_ATTR_DIRECTORY;
            set_cluster(entry_out, fs->root_cluster);
        }
        if (parent_dir_out) *parent_dir_out = fs->root_cluster;
        return 0;
    }
    
    char component[13]; /* 8.3 + dot + null */
    
    while (*p) {
        /* Extract next path component */
        int i = 0;
        while (*p && *p != '/' && i < 12) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        
        /* Skip trailing slashes */
        while (*p == '/') p++;
        
        /* Find this component in current directory */
        fat32_dir_entry_t entry;
        uint32_t found_cluster;
        int entry_index;
        
        if (fat32_find_entry(fs, current_dir, component, &entry, &found_cluster, &entry_index) != 0) {
            return -1; /* Not found */
        }
        
        if (*p) {
            /* More path components - this must be a directory */
            if (!(entry.attr & FAT32_ATTR_DIRECTORY)) {
                return -1; /* Not a directory */
            }
            current_dir = get_cluster(&entry);
        } else {
            /* Last component */
            if (entry_out) *entry_out = entry;
            if (parent_dir_out) *parent_dir_out = current_dir;
            return 0;
        }
    }
    
    /* Path ended at a directory */
    if (entry_out) {
        memset(entry_out, 0, sizeof(*entry_out));
        entry_out->attr = FAT32_ATTR_DIRECTORY;
        set_cluster(entry_out, current_dir);
    }
    if (parent_dir_out) *parent_dir_out = current_dir;
    return 0;
}

/* ====================================================================
 * High-level filesystem API
 * ==================================================================== */

int fat32_open(fat32_fs_t *fs, const char *path, fat32_dir_entry_t *entry_out, int create) {
    int result = fat32_resolve_path(fs, path, entry_out, NULL);
    
    if (result == 0) {
        return 0; /* Found */
    }
    
    if (!create) {
        return -1; /* Not found and not creating */
    }
    
    /* Need to create the file */
    /* Get parent directory */
    char parent_path[256];
    strncpy(parent_path, path, sizeof(parent_path) - 1);
    parent_path[sizeof(parent_path) - 1] = '\0';
    
    /* Find last slash */
    char *last_slash = strrchr(parent_path, '/');
    char *filename;
    uint32_t parent_dir;
    
    if (last_slash) {
        if (last_slash == parent_path) {
            /* Root directory */
            parent_dir = fs->root_cluster;
        } else {
            *last_slash = '\0';
            fat32_dir_entry_t parent_entry;
            if (fat32_resolve_path(fs, parent_path, &parent_entry, NULL) != 0) {
                return -1; /* Parent not found */
            }
            parent_dir = get_cluster(&parent_entry);
        }
        filename = last_slash + 1;
    } else {
        parent_dir = fs->root_cluster;
        filename = parent_path;
    }
    
    /* Create the file */
    if (fat32_create_file(fs, parent_dir, filename, 0, 0, FAT32_ATTR_ARCHIVE) != 0) {
        return -1;
    }
    
    /* Return the new entry */
    return fat32_find_entry(fs, parent_dir, filename, entry_out, NULL, NULL);
}

int fat32_write_path(fat32_fs_t *fs, const char *path, const void *data, size_t size) {
    fat32_dir_entry_t entry;
    uint32_t parent_dir;
    
    int found = fat32_resolve_path(fs, path, &entry, &parent_dir);
    
    if (found == 0) {
        /* File exists - update it */
        if (entry.attr & FAT32_ATTR_DIRECTORY) {
            return -1; /* Cannot write to a directory */
        }
        
        /* Free old cluster chain if any */
        uint32_t old_cluster = get_cluster(&entry);
        if (old_cluster >= 2) {
            fat32_free_chain(fs, old_cluster);
        }
    } else {
        /* File doesn't exist - create it */
        char parent_path[256];
        strncpy(parent_path, path, sizeof(parent_path) - 1);
        parent_path[sizeof(parent_path) - 1] = '\0';
        
        char *last_slash = strrchr(parent_path, '/');
        char *filename;
        
        if (last_slash) {
            if (last_slash == parent_path) {
                parent_dir = fs->root_cluster;
            } else {
                *last_slash = '\0';
                fat32_dir_entry_t parent_entry;
                if (fat32_resolve_path(fs, parent_path, &parent_entry, NULL) != 0) {
                    return -1;
                }
                parent_dir = get_cluster(&parent_entry);
            }
            filename = last_slash + 1;
        } else {
            parent_dir = fs->root_cluster;
            filename = parent_path;
        }
        
        /* Create empty file first */
        if (fat32_create_file(fs, parent_dir, filename, 0, 0, FAT32_ATTR_ARCHIVE) != 0) {
            return -1;
        }
        
        /* Find the entry we just created */
        int entry_index;
        if (fat32_find_entry(fs, parent_dir, filename, &entry, &parent_dir, &entry_index) != 0) {
            return -1;
        }
    }
    
    /* Write data to file */
    uint32_t new_start = 0;
    int written = fat32_write_file(fs, 0, data, size, &new_start);
    if (written < 0) {
        return -1;
    }
    
    /* Update the directory entry with new size and cluster */
    /* Re-find the entry to get its index */
    int entry_index;
    uint32_t found_dir;
    if (fat32_find_entry(fs, parent_dir, (const char *)entry.name, &entry, &found_dir, &entry_index) != 0) {
        return -1;
    }
    
    if (fat32_update_dir_entry(fs, found_dir, entry_index, size, new_start) != 0) {
        return -1;
    }
    
    return written;
}

int fat32_read_path(fat32_fs_t *fs, const char *path, void *buffer, size_t max_size) {
    fat32_dir_entry_t entry;
    
    if (fat32_resolve_path(fs, path, &entry, NULL) != 0) {
        return -1;
    }
    
    if (entry.attr & FAT32_ATTR_DIRECTORY) {
        return -1;
    }
    
    uint32_t start_cluster = get_cluster(&entry);
    size_t file_size = entry.file_size;
    
    if (file_size == 0) return 0;
    if (file_size > max_size) file_size = max_size;
    
    return fat32_read_file(fs, start_cluster, buffer, file_size, 0);
}

int fat32_delete_path(fat32_fs_t *fs, const char *path) {
    fat32_dir_entry_t entry;
    uint32_t parent_dir;
    
    if (fat32_resolve_path(fs, path, &entry, &parent_dir) != 0) {
        return -1;
    }
    
    char name[13];
    fat32_format_name(entry.name, name);
    
    if (entry.attr & FAT32_ATTR_DIRECTORY) {
        return fat32_rmdir(fs, parent_dir, name);
    } else {
        return fat32_unlink(fs, parent_dir, name);
    }
}

int fat32_mkdir_path(fat32_fs_t *fs, const char *path) {
    char parent_path[256];
    strncpy(parent_path, path, sizeof(parent_path) - 1);
    parent_path[sizeof(parent_path) - 1] = '\0';
    
    char *last_slash = strrchr(parent_path, '/');
    char *dirname;
    uint32_t parent_dir;
    
    if (last_slash) {
        if (last_slash == parent_path) {
            parent_dir = fs->root_cluster;
        } else {
            *last_slash = '\0';
            fat32_dir_entry_t parent_entry;
            if (fat32_resolve_path(fs, parent_path, &parent_entry, NULL) != 0) {
                return -1;
            }
            parent_dir = get_cluster(&parent_entry);
        }
        dirname = last_slash + 1;
    } else {
        parent_dir = fs->root_cluster;
        dirname = parent_path;
    }
    
    return fat32_mkdir(fs, parent_dir, dirname);
}

int fat32_list_dir_path(fat32_fs_t *fs, const char *path, fat32_list_callback_t callback, void *ctx) {
    fat32_dir_entry_t entry;
    uint32_t dir_cluster;
    
    if (fat32_resolve_path(fs, path, &entry, NULL) != 0) {
        return -1;
    }
    
    dir_cluster = get_cluster(&entry);
    if (!(entry.attr & FAT32_ATTR_DIRECTORY)) {
        return -1;
    }
    
    fat32_dir_entry_t entries[64];
    int count = fat32_read_dir(fs, dir_cluster, entries, 64);
    
    if (count < 0) return -1;
    
    for (int i = 0; i < count; i++) {
        char name[13];
        fat32_format_name(entries[i].name, name);
        if (callback) {
            callback(name, entries[i].file_size, entries[i].attr, ctx);
        }
    }
    
    return count;
}
