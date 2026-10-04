/* kernel/fat32.c — FAT32 Filesystem Driver
 * 
 * A basic read-only FAT32 driver for ArchForge OS.
 */
#include "fat32.h"
#include "ata.h"
#include "heap.h"
#include "serial.h"
#include "../include/string.h"

/* Helper to read a sector (with partition offset) */
static int read_sector(fat32_fs_t *fs, uint32_t lba, void *buffer) {
    return ata_read_sector(lba + fs->partition_lba, (uint8_t *)buffer, 1);
}

/* Helper to write a sector (with partition offset) */
static int write_sector(fat32_fs_t *fs, uint32_t lba, const void *buffer) {
    return ata_write_sector(lba + fs->partition_lba, (const uint8_t *)buffer, 1);
}

/* Helper to get cluster from directory entry */
static uint32_t get_cluster(fat32_dir_entry_t *entry) {
    return ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
}

/* Helper to get next cluster from FAT */
static uint32_t get_next_cluster(fat32_fs_t *fs, uint32_t cluster) {
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

/* Convert LFN/short name to standard string */
static void format_fat_name(const uint8_t *fat_name, char *out) {
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

/* Initialize FAT32 filesystem */
int fat32_init(int drive, fat32_fs_t *fs) {
    /* First, read the MBR (LBA 0) to find the FAT32 partition */
    uint8_t mbr[512];
    if (ata_read_sector(0, mbr, 1) != 0) {
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
    
    /* Allocate FAT buffer (optional, for caching) */
    fs->fat_buffer = NULL;
    
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
            if (read_sector(fs, sector + i, sec_buf) != 0) {
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
            if (read_sector(fs, sector + i, sec_buf) != 0) {
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