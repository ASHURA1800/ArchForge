/* kernel/fat32.c — FAT32 Filesystem Driver
 * 
 * A basic read-only FAT32 driver for ArchForge OS.
 * Extended with block cache and FAT cache for write support.
 */
#include "fat32.h"
#include "ata.h"
#include "heap.h"
#include "serial.h"
#include "../include/string.h"

/* Simple LRU timestamp counter */
static uint64_t fat32_lru_counter = 0;

/* ====================================================================
 * Internal helper functions (declared first to avoid forward declarations)
 * ==================================================================== */

/* Helper to read a sector (with partition offset) */
static int read_sector(fat32_fs_t *fs, uint32_t lba, void *buffer) {
    /* Use the drive where the partition was found */
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

/* Helper to get next cluster from FAT (uses cache if available) */
static uint32_t get_next_cluster(fat32_fs_t *fs, uint32_t cluster) {
    /* Use FAT cache if available */
    if (fs->fat_buffer) {
        return fat32_get_fat_entry(fs, cluster);
    }
    
    /* Fallback to disk read */
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
    uint32_t fat_entry_offset = fat_offset % fs->bytes_per_sector;
    
    uint8_t sector[512];
    if (read_sector(fs, fat_sector, sector) != 0) {
        return 0x0FFFFFFF; /* Error */
    }
    
    uint32_t next_cluster = *(uint32_t *)(sector + fat_entry_offset);
    return next_cluster & 0x0FFFFFFF;
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
            return i; /* Cache hit */
        }
        if (fs->block_cache[i].last_access < oldest) {
            oldest = fs->block_cache[i].last_access;
            oldest_slot = i;
        }
    }
    
    /* Return empty slot if available, otherwise LRU slot */
    return (empty_slot >= 0) ? empty_slot : oldest_slot;
}

/* ====================================================================
 * Cache management functions
 * ==================================================================== */

/* Helper to read a sector through cache */
int fat32_cache_read_sector(fat32_fs_t *fs, uint32_t lba, void *buffer) {
    fat32_lru_counter++;
    
    int slot = fat32_find_cache_slot(fs, lba);
    if (slot < 0) return -1;
    
    if (fs->block_cache[slot].valid && fs->block_cache[slot].sector_lba == lba) {
        /* Cache hit */
        fs->cache_hits++;
        fs->block_cache[slot].last_access = fat32_lru_counter;
        memcpy(buffer, fs->block_cache[slot].data, fs->bytes_per_sector);
        return 0;
    }
    
    /* Cache miss - load from disk */
    fs->cache_misses++;
    
    /* Allocate buffer if needed */
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

/* Helper to write a sector through cache (write-through) */
int fat32_cache_write_sector(fat32_fs_t *fs, uint32_t lba, const void *buffer) {
    fat32_lru_counter++;
    
    int slot = fat32_find_cache_slot(fs, lba);
    if (slot < 0) return -1;
    
    /* Allocate buffer if needed */
    if (!fs->block_cache[slot].data) {
        fs->block_cache[slot].data = kmalloc(fs->bytes_per_sector);
        if (!fs->block_cache[slot].data) return -1;
    }
    
    /* Write-through: write to disk immediately */
    if (write_sector(fs, lba, buffer) != 0) {
        return -1;
    }
    
    /* Update cache */
    memcpy(fs->block_cache[slot].data, buffer, fs->bytes_per_sector);
    fs->block_cache[slot].sector_lba = lba;
    fs->block_cache[slot].valid = 1;
    fs->block_cache[slot].dirty = 0;
    fs->block_cache[slot].last_access = fat32_lru_counter;
    
    return 0;
}

/* Flush all dirty cache entries */
void fat32_cache_flush(fat32_fs_t *fs) {
    for (int i = 0; i < 8; i++) {
        if (fs->block_cache[i].valid && fs->block_cache[i].dirty) {
            write_sector(fs, fs->block_cache[i].sector_lba, fs->block_cache[i].data);
            fs->block_cache[i].dirty = 0;
        }
    }
}

/* Get cache statistics */
void fat32_cache_stats(fat32_fs_t *fs, uint32_t *hits, uint32_t *misses) {
    if (hits) *hits = fs->cache_hits;
    if (misses) *misses = fs->cache_misses;
}

/* Load entire FAT into cache */
int fat32_load_fat_cache(fat32_fs_t *fs) {
    size_t fat_size = fs->fat_size_32 * fs->bytes_per_sector;
    fs->fat_buffer = kmalloc(fat_size * fs->num_fats); /* Cache all FAT copies */
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

/* Get FAT entry from cache */
uint32_t fat32_get_fat_entry(fat32_fs_t *fs, uint32_t cluster) {
    if (!fs->fat_buffer) return 0x0FFFFFFF;
    
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_offset / fs->bytes_per_sector;
    uint32_t fat_entry_offset = fat_offset % fs->bytes_per_sector;
    
    /* Use first FAT copy */
    uint8_t *fat_data = fs->fat_buffer;
    return *(uint32_t *)(fat_data + fat_sector * fs->bytes_per_sector + fat_entry_offset) & 0x0FFFFFFF;
}

/* Set FAT entry in cache and write-through to all FAT copies */
int fat32_set_fat_entry(fat32_fs_t *fs, uint32_t cluster, uint32_t value) {
    if (!fs->fat_buffer) return -1;
    
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_offset / fs->bytes_per_sector;
    uint32_t fat_entry_offset = fat_offset % fs->bytes_per_sector;
    
    value &= 0x0FFFFFFF;
    
    for (uint8_t fat = 0; fat < fs->num_fats; fat++) {
        uint32_t fat_start = fs->fat_start_sector + fat * fs->fat_size_32;
        uint8_t sector_buf[512];
        
        /* Read current sector */
        if (read_sector(fs, fat_start + fat_sector, sector_buf) != 0) return -1;
        
        /* Update entry */
        *(uint32_t *)(sector_buf + fat_entry_offset) = value;
        
        /* Write back */
        if (write_sector(fs, fat_start + fat_sector, sector_buf) != 0) return -1;
    }
    
    /* Update cache */
    uint8_t *fat_data = fs->fat_buffer;
    *(uint32_t *)(fat_data + fat_sector * fs->bytes_per_sector + fat_entry_offset) = value;
    
    return 0;
}

/* Convert LFN/short name to standard string */
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
    
    /* Convert to lowercase */
    for (i = 0; i < j; i++) {
        if (out[i] >= 'A' && out[i] <= 'Z') {
            out[i] = out[i] + 32;
        }
    }
}

/* ====================================================================
 * Public API functions
 * ==================================================================== */

/* Initialize FAT32 filesystem */
int fat32_init(int drive, fat32_fs_t *fs) {
    /* First, read the MBR (LBA 0) to find the FAT32 partition */
    uint8_t mbr[512];
    if (ata_read_sector(drive, 0, mbr, 1) != 0) {
        return -1;
    }
    
    /* Check MBR signature */
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        return -1;
    }
    
    /* Parse partition table (4 entries at offset 0x1BE) */
    uint32_t partition_lba = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t *entry = mbr + 0x1BE + i * 16;
        uint8_t type = entry[4];
        /* FAT32 partition types: 0x0B, 0x0C, 0x1B, 0x1C */
        if (type == 0x0B || type == 0x0C || type == 0x1B || type == 0x1C) {
            /* Read partition start LBA (little endian, 4 bytes at offset 8) */
            partition_lba = *(uint32_t *)(entry + 8);
            break;
        }
    }
    
    if (partition_lba == 0) {
        return -1; /* No FAT32 partition found */
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
    
    /* Now read the BPB from the partition start */
    fat32_bpb_t bpb;
    if (read_sector(fs, 0, &bpb) != 0) {
        return -1;
    }
    
    /* Validate FAT32 signature */
    if (bpb.boot_signature != 0x29 && bpb.boot_signature != 0x28) {
        /* Try checking fs_type string */
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
    
    /* Load FAT into cache */
    if (fat32_load_fat_cache(fs) != 0) {
        serial_write("[FAT32] WARNING: Failed to load FAT cache\n");
    }
    
    serial_write("[FAT32] Initialized on drive ");
    char drive_str[4];
    drive_str[0] = '0' + drive;
    drive_str[1] = '\0';
    serial_write(drive_str);
    serial_write(" (partition LBA: ");
    serial_write_hex(partition_lba);
    serial_write(")\n");
    
    return 0;
}

/* Read file data from FAT32 */
int fat32_read_file(fat32_fs_t *fs, uint32_t cluster, void *buffer, size_t size, size_t offset) {
    if (cluster < 2 || cluster > 0x0FFFFFFF) return -1;
    
    uint8_t *buf = (uint8_t *)buffer;
    size_t bytes_read = 0;
    size_t current_offset = 0;
    
    while (cluster >= 2 && cluster <= 0x0FFFFFF6) {
        uint32_t sector = fs->first_data_sector + (cluster - 2) * fs->sectors_per_cluster;
        
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

/* Read directory entries */
int fat32_read_dir(fat32_fs_t *fs, uint32_t cluster, void *buffer, size_t max_entries) {
    if (cluster < 2 || cluster > 0x0FFFFFFF) return -1;
    
    fat32_dir_entry_t *entries = (fat32_dir_entry_t *)buffer;
    size_t entry_count = 0;
    
    while (cluster >= 2 && cluster <= 0x0FFFFFF6) {
        uint32_t sector = fs->first_data_sector + (cluster - 2) * fs->sectors_per_cluster;
        
        for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) {
            uint8_t sec_buf[512];
            if (fat32_cache_read_sector(fs, sector + i, sec_buf) != 0) {
                return -1;
            }
            
            for (size_t j = 0; j < 512 / sizeof(fat32_dir_entry_t); j++) {
                fat32_dir_entry_t *entry = (fat32_dir_entry_t *)(sec_buf + j * sizeof(fat32_dir_entry_t));
                
                /* End of directory */
                if (entry->name[0] == 0x00) {
                    return entry_count;
                }
                
                /* Deleted entry */
                if (entry->name[0] == 0xE5) {
                    continue;
                }
                
                /* Skip volume label and LFN entries */
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
 * Cluster Allocator
 * ==================================================================== */

/* Count free clusters in FAT */
uint32_t fat32_count_free_clusters(fat32_fs_t *fs) {
    if (!fs->fat_buffer) return 0;
    
    uint32_t free_count = 0;
    uint32_t total_clusters = (fs->fat_size_32 * fs->bytes_per_sector) / 4;
    
    for (uint32_t cluster = 2; cluster < total_clusters; cluster++) {
        if (fat32_get_fat_entry(fs, cluster) == 0x00000000) {
            free_count++;
        }
    }
    
    return free_count;
}

/* Allocate a free cluster */
uint32_t fat32_alloc_cluster(fat32_fs_t *fs) {
    if (!fs->fat_buffer) return 0;
    
    uint32_t total_clusters = (fs->fat_size_32 * fs->bytes_per_sector) / 4;
    
    /* Start from cluster 2 (first data cluster) or use FSInfo hint */
    uint32_t start_cluster = 2;
    
    for (uint32_t cluster = start_cluster; cluster < total_clusters; cluster++) {
        if (fat32_get_fat_entry(fs, cluster) == 0x00000000) {
            /* Found free cluster - mark as EOC (End of Chain) */
            if (fat32_set_fat_entry(fs, cluster, 0x0FFFFFFF) != 0) {
                return 0;
            }
            
            /* Update FSInfo sector if we have it cached */
            /* TODO: Implement FSInfo update */
            
            return cluster;
        }
    }
    
    return 0; /* No free clusters */
}

/* Free a single cluster */
int fat32_free_cluster(fat32_fs_t *fs, uint32_t cluster) {
    if (!fs->fat_buffer) return -1;
    if (cluster < 2) return -1;
    
    uint32_t total_clusters = (fs->fat_size_32 * fs->bytes_per_sector) / 4;
    if (cluster >= total_clusters) return -1;
    
    /* Mark cluster as free (0) */
    if (fat32_set_fat_entry(fs, cluster, 0x00000000) != 0) {
        return -1;
    }
    
    return 0;
}

/* Free an entire cluster chain */
int fat32_free_chain(fat32_fs_t *fs, uint32_t start_cluster) {
    if (!fs->fat_buffer) return -1;
    if (start_cluster < 2) return -1;
    
    uint32_t total_clusters = (fs->fat_size_32 * fs->bytes_per_sector) / 4;
    if (start_cluster >= total_clusters) return -1;
    
    uint32_t cluster = start_cluster;
    while (cluster >= 2 && cluster <= 0x0FFFFFF6) {
        uint32_t next = fat32_get_fat_entry(fs, cluster);
        
        /* Mark current cluster as free */
        if (fat32_set_fat_entry(fs, cluster, 0x00000000) != 0) {
            return -1;
        }
        
        cluster = next;
    }
    
    return 0;
}