/* kernel/ata.c — ATA PIO Driver
 * 
 * Basic ATA PIO mode driver for reading/writing sectors.
 */
#include "ata.h"
#include "io.h"
#include "serial.h"

static uint8_t ata_device = 0; /* 0 = Primary Master, 1 = Primary Slave */

/* Wait for ATA drive to be ready */
static int ata_wait_ready(void) {
    uint8_t status;
    int timeout = 100000;
    do {
        status = inb(ATA_PRIMARY_STATUS);
        if (!(status & ATA_SR_BSY)) break;
        timeout--;
    } while (timeout > 0);
    
    if (timeout == 0) {
        serial_write("[ATA] Timeout waiting for drive ready\n");
        return -1;
    }
    return 0;
}

/* Wait for DRQ (Data Request) */
static int ata_wait_drq(void) {
    uint8_t status;
    int timeout = 100000;
    do {
        status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_SR_DRQ) break;
        if (status & ATA_SR_ERR) {
            serial_write("[ATA] Error waiting for DRQ\n");
            return -1;
        }
        timeout--;
    } while (timeout > 0);
    
    if (timeout == 0) {
        serial_write("[ATA] Timeout waiting for DRQ\n");
        return -1;
    }
    return 0;
}

void ata_init(void) {
    serial_write("[ATA] Initializing ATA PIO driver...\n");
    
    /* Select drive 0 (Primary Master) */
    outb(ATA_PRIMARY_DRIVE, 0xA0);
    
    /* Wait for drive to be ready */
    if (ata_wait_ready() == 0) {
        serial_write("[ATA] Primary Master detected and ready.\n");
        ata_device = 0;
    } else {
        serial_write("[ATA] WARNING: No ATA drive detected.\n");
    }
}

int ata_read_sector(uint32_t lba, uint8_t *buffer, uint32_t count) {
    if (!buffer || count == 0) return -1;
    
    /* Wait for drive to be ready */
    if (ata_wait_ready() != 0) return -1;
    
    /* Set up LBA and count */
    outb(ATA_PRIMARY_SECCOUNT, count & 0xFF);
    outb(ATA_PRIMARY_LBA_LOW, lba & 0xFF);
    outb(ATA_PRIMARY_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_LBA_HIGH, (lba >> 16) & 0xFF);
    
    /* Select drive and set LBA mode (bit 6 = 1) */
    uint8_t drive_reg = 0xE0 | ((ata_device & 1) << 4) | ((lba >> 24) & 0x0F);
    outb(ATA_PRIMARY_DRIVE, drive_reg);
    
    /* Send read command */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_SECTORS);
    
    /* Wait for data */
    if (ata_wait_drq() != 0) return -1;
    
    /* Read data (256 words per sector) */
    uint16_t *buf16 = (uint16_t *)buffer;
    for (uint32_t c = 0; c < count; c++) {
        for (int i = 0; i < 256; i++) {
            *buf16++ = inw(ATA_PRIMARY_DATA);
        }
        /* Wait for next sector if count > 1 */
        if (c < count - 1) {
            if (ata_wait_drq() != 0) return -1;
        }
    }
    
    return 0;
}

int ata_write_sector(uint32_t lba, const uint8_t *buffer, uint32_t count) {
    if (!buffer || count == 0) return -1;
    
    if (ata_wait_ready() != 0) return -1;
    
    outb(ATA_PRIMARY_SECCOUNT, count & 0xFF);
    outb(ATA_PRIMARY_LBA_LOW, lba & 0xFF);
    outb(ATA_PRIMARY_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_LBA_HIGH, (lba >> 16) & 0xFF);
    
    uint8_t drive_reg = 0xE0 | ((ata_device & 1) << 4) | ((lba >> 24) & 0x0F);
    outb(ATA_PRIMARY_DRIVE, drive_reg);
    
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);
    
    if (ata_wait_drq() != 0) return -1;
    
    const uint16_t *buf16 = (const uint16_t *)buffer;
    for (uint32_t c = 0; c < count; c++) {
        for (int i = 0; i < 256; i++) {
            outw(ATA_PRIMARY_DATA, *buf16++);
        }
        if (c < count - 1) {
            if (ata_wait_drq() != 0) return -1;
        }
    }
    
    /* Wait for write to complete */
    return ata_wait_ready();
}