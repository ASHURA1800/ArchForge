#ifndef ARCHFORGE_ATA_H
#define ARCHFORGE_ATA_H

#include <stdint.h>
#include <stddef.h>

/* ATA ports for Primary Master */
#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERROR       0x1F1
#define ATA_PRIMARY_SECCOUNT    0x1F2
#define ATA_PRIMARY_LBA_LOW     0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HIGH    0x1F5
#define ATA_PRIMARY_DRIVE       0x1F6
#define ATA_PRIMARY_STATUS      0x1F7
#define ATA_PRIMARY_COMMAND     0x1F7

/* ATA Commands */
#define ATA_CMD_READ_SECTORS    0x20
#define ATA_CMD_WRITE_SECTORS   0x30
#define ATA_CMD_IDENTIFY        0xEC

/* ATA Status bits */
#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

void ata_init(void);
int ata_read_sector(uint32_t lba, uint8_t *buffer, uint32_t count);
int ata_write_sector(uint32_t lba, const uint8_t *buffer, uint32_t count);

#endif /* ARCHFORGE_ATA_H */