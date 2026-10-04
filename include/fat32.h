#ifndef ARCHFORGE_FAT32_H
#define ARCHFORGE_FAT32_H

#include <stdint.h>
#include <stddef.h>

/* FAT32 BIOS Parameter Block (BPB) */
typedef struct __attribute__((packed)) {
    uint8_t  jmp_boot[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    
    /* FAT32 specific */
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} fat32_bpb_t;

/* FAT32 Directory Entry */
typedef struct __attribute__((packed)) {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  ctime_cs;
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t cluster_high;
    uint16_t mtime;
    uint16_t mdate;
    uint16_t cluster_low;
    uint32_t file_size;
} fat32_dir_entry_t;

/* FAT32 Filesystem Context */
typedef struct {
    int drive;
    uint32_t first_data_sector;
    uint32_t sectors_per_cluster;
    uint32_t root_cluster;
    uint32_t bytes_per_sector;
    uint8_t  num_fats;
    uint32_t fat_size_32;
    uint32_t fat_start_sector;
    uint8_t *fat_buffer; /* Cached FAT table */
    uint32_t partition_lba; /* Partition start LBA (for MBR) */

    /* Block cache (write-through) */
    struct {
        uint32_t sector_lba;
        uint8_t  *data;
        uint8_t  valid;
        uint8_t  dirty;
        uint64_t last_access;
    } block_cache[8];
    uint32_t cache_hits;
    uint32_t cache_misses;
} fat32_fs_t;

/* Initialize FAT32 filesystem on given drive */
int fat32_init(int drive, fat32_fs_t *fs);

/* Read a file from FAT32 */
int fat32_read_file(fat32_fs_t *fs, uint32_t cluster, void *buffer, size_t size, size_t offset);

/* Read directory entries */
int fat32_read_dir(fat32_fs_t *fs, uint32_t cluster, void *buffer, size_t max_entries);

/* Convert FAT name to standard string */
void fat32_format_name(const uint8_t *fat_name, char *out);

/* Cache management */
int fat32_cache_read_sector(fat32_fs_t *fs, uint32_t lba, void *buffer);
int fat32_cache_write_sector(fat32_fs_t *fs, uint32_t lba, const void *buffer);
void fat32_cache_flush(fat32_fs_t *fs);
void fat32_cache_stats(fat32_fs_t *fs, uint32_t *hits, uint32_t *misses);

/* FAT cache management */
int fat32_load_fat_cache(fat32_fs_t *fs);
uint32_t fat32_get_fat_entry(fat32_fs_t *fs, uint32_t cluster);
int fat32_set_fat_entry(fat32_fs_t *fs, uint32_t cluster, uint32_t value);

#endif