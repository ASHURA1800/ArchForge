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
    uint32_t total_clusters; /* Total number of clusters */
    uint32_t next_free_cluster; /* Hint for next free cluster (FSInfo) */
    uint32_t free_cluster_count; /* Hint for free cluster count (FSInfo) */

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

/* Directory attributes */
#define FAT32_ATTR_READ_ONLY  0x01
#define FAT32_ATTR_HIDDEN     0x02
#define FAT32_ATTR_SYSTEM     0x04
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_ARCHIVE    0x20
#define FAT32_ATTR_LONG_NAME  0x0F

/* FAT32 special cluster values */
#define FAT32_EOC           0x0FFFFFF8  /* End of chain (minimum) */
#define FAT32_EOC_MAX       0x0FFFFFFF  /* End of chain (maximum) */
#define FAT32_FREE          0x00000000  /* Free cluster */
#define FAT32_BAD           0x0FFFFFF7  /* Bad cluster */

/* ====================================================================
 * Initialization
 * ==================================================================== */

/* Initialize FAT32 filesystem on given drive */
int fat32_init(int drive, fat32_fs_t *fs);

/* ====================================================================
 * Read operations
 * ==================================================================== */

/* Read a file from FAT32 */
int fat32_read_file(fat32_fs_t *fs, uint32_t cluster, void *buffer, size_t size, size_t offset);

/* Read directory entries */
int fat32_read_dir(fat32_fs_t *fs, uint32_t cluster, void *buffer, size_t max_entries);

/* Convert FAT name to standard string */
void fat32_format_name(const uint8_t *fat_name, char *out);

/* ====================================================================
 * Cache management
 * ==================================================================== */

/* Read/write through block cache */
int fat32_cache_read_sector(fat32_fs_t *fs, uint32_t lba, void *buffer);
int fat32_cache_write_sector(fat32_fs_t *fs, uint32_t lba, const void *buffer);
void fat32_cache_flush(fat32_fs_t *fs);
void fat32_cache_stats(fat32_fs_t *fs, uint32_t *hits, uint32_t *misses);

/* FAT cache management */
int fat32_load_fat_cache(fat32_fs_t *fs);
uint32_t fat32_get_fat_entry(fat32_fs_t *fs, uint32_t cluster);
int fat32_set_fat_entry(fat32_fs_t *fs, uint32_t cluster, uint32_t value);

/* ====================================================================
 * A3: Cluster allocator
 * ==================================================================== */

/* Allocate a free cluster (marks it as EOC) */
uint32_t fat32_alloc_cluster(fat32_fs_t *fs);

/* Free a single cluster */
int fat32_free_cluster(fat32_fs_t *fs, uint32_t cluster);

/* Free an entire cluster chain starting from start_cluster */
int fat32_free_chain(fat32_fs_t *fs, uint32_t start_cluster);

/* Count free clusters in the filesystem */
uint32_t fat32_count_free_clusters(fat32_fs_t *fs);

/* ====================================================================
 * A4: Chain operations
 * ==================================================================== */

/* Get the length of a cluster chain */
uint32_t fat32_chain_length(fat32_fs_t *fs, uint32_t start_cluster);

/* Get the last cluster in a chain */
uint32_t fat32_chain_last(fat32_fs_t *fs, uint32_t start_cluster);

/* Extend a chain by allocating a new cluster and linking it to last_cluster.
 * Returns the new cluster number, or 0 on failure. */
uint32_t fat32_extend_chain(fat32_fs_t *fs, uint32_t last_cluster);

/* Truncate a chain: keep the first 'keep_count' clusters, free the rest.
 * Returns 0 on success, -1 on failure. */
int fat32_truncate_chain(fat32_fs_t *fs, uint32_t start_cluster, uint32_t keep_count);

/* Ensure a chain has at least 'needed' clusters. Allocates more if needed.
 * Returns the start cluster (unchanged) or 0 on failure. */
uint32_t fat32_ensure_chain(fat32_fs_t *fs, uint32_t start_cluster, uint32_t needed);

/* ====================================================================
 * A5: File write operations
 * ==================================================================== */

/* Write data to a file's cluster chain.
 * start_cluster: first cluster of the file (0 for new empty file)
 * data: data to write
 * size: number of bytes to write
 * new_start: output - new start cluster (may be allocated if start_cluster was 0)
 * Returns bytes written, or -1 on error.
 * Allocates new clusters as needed. */
int fat32_write_file(fat32_fs_t *fs, uint32_t start_cluster, 
                     const void *data, size_t size, uint32_t *new_start);

/* ====================================================================
 * A6: Directory entry operations
 * ==================================================================== */

/* Convert a standard filename to 8.3 short name format */
void fat32_make_short_name(const char *name, uint8_t *short_name);

/* Find a directory entry by name.
 * Returns 0 on success, -1 if not found.
 * If found, fills out entry_out, dir_cluster_out, and entry_index_out. */
int fat32_find_entry(fat32_fs_t *fs, uint32_t dir_cluster, const char *name,
                     fat32_dir_entry_t *entry_out, 
                     uint32_t *dir_cluster_out, int *entry_index_out);

/* Create a file in the given directory.
 * Returns 0 on success, -1 on error. */
int fat32_create_file(fat32_fs_t *fs, uint32_t parent_dir, const char *name,
                      uint32_t start_cluster, uint32_t size, uint8_t attr);

/* Update a directory entry (e.g., after writing to a file).
 * dir_cluster: the cluster containing the entry
 * entry_index: index within that cluster's sectors
 * new_size: new file size
 * new_start_cluster: new start cluster */
int fat32_update_dir_entry(fat32_fs_t *fs, uint32_t dir_cluster, 
                           int entry_index, uint32_t new_size,
                           uint32_t new_start_cluster);

/* ====================================================================
 * A7: Filesystem operations
 * ==================================================================== */

/* Create a directory */
int fat32_mkdir(fat32_fs_t *fs, uint32_t parent_dir, const char *name);

/* Delete a file (unlink) */
int fat32_unlink(fat32_fs_t *fs, uint32_t dir_cluster, const char *name);

/* Delete a directory (must be empty) */
int fat32_rmdir(fat32_fs_t *fs, uint32_t dir_cluster, const char *name);

/* Rename a file or directory */
int fat32_rename(fat32_fs_t *fs, uint32_t dir_cluster, 
                 const char *old_name, const char *new_name);

/* Resolve a full path (e.g., "/dir1/dir2/file.txt") to a directory entry.
 * Returns 0 on success, fills out entry_out.
 * If parent_dir_out is non-NULL, returns the parent directory cluster. */
int fat32_resolve_path(fat32_fs_t *fs, const char *path,
                       fat32_dir_entry_t *entry_out,
                       uint32_t *parent_dir_out);

/* ====================================================================
 * High-level filesystem API (for shell/VFS integration)
 * ==================================================================== */

/* Open or create a file by path. Returns 0 on success.
 * If create=1 and file doesn't exist, creates it. */
int fat32_open(fat32_fs_t *fs, const char *path, fat32_dir_entry_t *entry_out, int create);

/* Write data to a file by path. Creates the file if it doesn't exist. */
int fat32_write_path(fat32_fs_t *fs, const char *path, const void *data, size_t size);

/* Read a file by path into buffer. Returns bytes read. */
int fat32_read_path(fat32_fs_t *fs, const char *path, void *buffer, size_t max_size);

/* Delete a file by path */
int fat32_delete_path(fat32_fs_t *fs, const char *path);

/* Create a directory by path */
int fat32_mkdir_path(fat32_fs_t *fs, const char *path);

/* List directory by path. Calls callback for each entry. */
typedef void (*fat32_list_callback_t)(const char *name, uint32_t size, uint8_t attr, void *ctx);
int fat32_list_dir_path(fat32_fs_t *fs, const char *path, fat32_list_callback_t callback, void *ctx);

/* ====================================================================
 * Global filesystem instance (A9: VFS integration)
 * ==================================================================== */

/* Mount the first available FAT32 partition (scans all drives)
 * Returns 0 on success, -1 if no FAT32 partition found */
int fat32_mount(void);

/* Get the global filesystem instance (NULL if not mounted) */
fat32_fs_t *fat32_get_fs(void);

/* Check if FAT32 is mounted */
int fat32_is_mounted(void);

/* Flush all caches to disk (important before shutdown) */
void fat32_sync(void);

#endif
