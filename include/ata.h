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

/* ATA Commands */
#define ATA_CMD_READ_SECTORS     0x20
#define ATA_CMD_WRITE_SECTORS    0x30
#define ATA_CMD_IDENTIFY         0xEC

/* ATA Status bits */
#define ATA_SR_BSY               0x80
#define ATA_SR_DRQ               0x08
#define ATA_SR_ERR               0x01

void ata_init(void);
int ata_read_sector(uint8_t drive, uint32_t lba, uint8_t *buffer, uint32_t count);
int ata_write_sector(uint8_t drive, uint32_t lba, const uint8_t *buffer, uint32_t count);

#endif /* ARCHFORGE_ATA_H */