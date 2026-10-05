#ifndef ARCHFORGE_ATA_H
#define ARCHFORGE_ATA_H

#include <stdint.h>
#include <stddef.h>

/* ATA ports for Primary channel (0x1F0-0x1F7) */
#define ATA_PRIMARY_BASE         0x1F0
#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERROR        0x1F1
#define ATA_PRIMARY_SECCOUNT     0x1F2
#define ATA_PRIMARY_LBA_LOW      0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HIGH     0x1F5
#define ATA_PRIMARY_DRIVE        0x1F6
#define ATA_PRIMARY_STATUS       0x1F7
#define ATA_PRIMARY_COMMAND      0x1F7

/* ATA ports for Secondary channel (0x170-0x177) */
#define ATA_SECONDARY_BASE       0x170
#define ATA_SECONDARY_DATA       0x170
#define ATA_SECONDARY_ERROR      0x171
#define ATA_SECONDARY_SECCOUNT   0x172
#define ATA_SECONDARY_LBA_LOW    0x173
#define ATA_SECONDARY_LBA_MID    0x174
#define ATA_SECONDARY_LBA_HIGH   0x175
#define ATA_SECONDARY_DRIVE      0x176
#define ATA_SECONDARY_STATUS     0x177
#define ATA_SECONDARY_COMMAND    0x177

/* Offset macros for channel-agnostic access */
#define ATA_DATA_OFFSET          0x00
#define ATA_ERROR_OFFSET         0x01
#define ATA_SECCOUNT_OFFSET      0x02
#define ATA_LBA_LOW_OFFSET       0x03
#define ATA_LBA_MID_OFFSET       0x04
#define ATA_LBA_HIGH_OFFSET      0x05
#define ATA_DRIVE_OFFSET         0x06
#define ATA_STATUS_OFFSET        0x07
#define ATA_COMMAND_OFFSET       0x07

/* Initialize ATA subsystem and identify all drives */
void ata_init(void);

/* Read sectors from drive (28-bit LBA)
 * drive: 0-3 (Primary Master, Primary Slave, Secondary Master, Secondary Slave)
 * lba: starting sector number
 * buffer: output buffer (must be at least count * 512 bytes)
 * count: number of sectors to read (1-255)
 * Returns 0 on success, -1 on error (with retry logic) */
int ata_read_sector(uint8_t drive, uint32_t lba, uint8_t *buffer, uint32_t count);

/* Write sectors to drive (28-bit LBA)
 * drive: 0-3
 * lba: starting sector number
 * buffer: input data
 * count: number of sectors to write (1-255)
 * Returns 0 on success, -1 on error (with retry logic) */
int ata_write_sector(uint8_t drive, uint32_t lba, const uint8_t *buffer, uint32_t count);

/* Flush drive's write cache to media
 * Important for data integrity before shutdown/reboot */
int ata_flush_cache(uint8_t drive);

/* Query drive information */
int ata_is_present(uint8_t drive);
uint32_t ata_get_total_sectors(uint8_t drive);
int ata_is_atapi(uint8_t drive);

#endif /* ARCHFORGE_ATA_H */
