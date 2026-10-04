/* kernel/ata.c — ATA PIO Driver
 * 
 * Basic ATA PIO mode driver for reading/writing sectors.
 * Supports 4 drives: Primary Master (0), Primary Slave (1), Secondary Master (2), Secondary Slave (3)
 */
#include "ata.h"
#include "io.h"
#include "serial.h"

static uint8_t ata_drives[4] = {0, 0, 0, 0}; /* 0 = not present, 1 = present */

/* Wait for ATA drive to be ready */
static int ata_wait_ready(uint8_t drive) {
    uint8_t status;
    int timeout = 100000;
    uint16_t base = (drive < 2) ? ATA_PRIMARY_BASE : ATA_SECONDARY_BASE;
    do {
        status = inb(base + ATA_STATUS_OFFSET);
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
static int ata_wait_drq(uint8_t drive) {
    uint8_t status;
    int timeout = 100000;
    uint16_t base = (drive < 2) ? ATA_PRIMARY_BASE : ATA_SECONDARY_BASE;
    do {
        status = inb(base + ATA_STATUS_OFFSET);
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
    
    /* Try Primary Master (drive 0) */
    if (ata_wait_ready(0) == 0) {
        serial_write("[ATA] Primary Master detected and ready.\n");
        ata_drives[0] = 1;
    } else {
        serial_write("[ATA] Primary Master not detected.\n");
    }
    
    /* Try Primary Slave (drive 1) */
    if (ata_wait_ready(1) == 0) {
        serial_write("[ATA] Primary Slave detected and ready.\n");
        ata_drives[1] = 1;
    } else {
        serial_write("[ATA] Primary Slave not detected.\n");
    }
    
    /* Try Secondary Master (drive 2) */
    /* In QEMU with -drive file=...,if=ide, the drive is typically secondary master */
    if (ata_wait_ready(2) == 0) {
        serial_write("[ATA] Secondary Master detected and ready.\n");
        ata_drives[2] = 1;
    } else {
        serial_write("[ATA] Secondary Master not detected.\n");
    }
    
    /* Try Secondary Slave (drive 3) */
    if (ata_wait_ready(3) == 0) {
        serial_write("[ATA] Secondary Slave detected and ready.\n");
        ata_drives[3] = 1;
    } else {
        serial_write("[ATA] Secondary Slave not detected.\n");
    }
}

/* Helper to get drive base port */
static uint16_t ata_get_base(uint8_t drive) {
    return (drive < 2) ? ATA_PRIMARY_BASE : ATA_SECONDARY_BASE;
}

int ata_read_sector(uint8_t drive, uint32_t lba, uint8_t *buffer, uint32_t count) {
    if (!buffer || count == 0 || drive > 3) return -1;
    
    if (!ata_drives[drive]) {
        serial_write("[ATA] Drive not initialized\n");
        return -1;
    }
    
    /* Wait for drive to be ready */
    if (ata_wait_ready(drive) != 0) return -1;
    
    uint16_t base = (drive < 2) ? ATA_PRIMARY_BASE : ATA_SECONDARY_BASE;
    
    /* Set up LBA and count */
    outb(base + ATA_SECCOUNT_OFFSET, count & 0xFF);
    outb(base + ATA_LBA_LOW_OFFSET, lba & 0xFF);
    outb(base + ATA_LBA_MID_OFFSET, (lba >> 8) & 0xFF);
    outb(base + ATA_LBA_HIGH_OFFSET, (lba >> 16) & 0xFF);
    
    /* Select drive and set LBA mode (bit 6 = 1) */
    uint8_t drive_reg;
    if (drive < 2) {
        drive_reg = 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F);
    } else {
        drive_reg = 0xE0 | ((drive - 2) << 4) | ((lba >> 24) & 0x0F);
    }
    outb(base + ATA_DRIVE_OFFSET, drive_reg);
    
    /* Send read command */
    outb(base + ATA_COMMAND_OFFSET, ATA_CMD_READ_SECTORS);
    
    /* Wait for data */
    if (ata_wait_drq(drive) != 0) return -1;
    
    /* Read data (256 words per sector) */
    uint16_t *buf16 = (uint16_t *)buffer;
    for (uint32_t c = 0; c < count; c++) {
        for (int i = 0; i < 256; i++) {
            *buf16++ = inw(base + ATA_DATA_OFFSET);
        }
        /* Wait for next sector if count > 1 */
        if (c < count - 1) {
            if (ata_wait_drq(drive) != 0) return -1;
        }
    }
    
    return 0;
}

int ata_write_sector(uint8_t drive, uint32_t lba, const uint8_t *buffer, uint32_t count) {
    if (!buffer || count == 0 || drive > 3) return -1;
    
    if (!ata_drives[drive]) return -1;
    
    if (ata_wait_ready(drive) != 0) return -1;
    
    uint16_t base = (drive < 2) ? ATA_PRIMARY_BASE : ATA_SECONDARY_BASE;
    
    outb(base + ATA_SECCOUNT_OFFSET, count & 0xFF);
    outb(base + ATA_LBA_LOW_OFFSET, lba & 0xFF);
    outb(base + ATA_LBA_MID_OFFSET, (lba >> 8) & 0xFF);
    outb(base + ATA_LBA_HIGH_OFFSET, (lba >> 16) & 0xFF);
    
    uint8_t drive_reg;
    if (drive < 2) {
        drive_reg = 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F);
    } else {
        drive_reg = 0xE0 | ((drive - 2) << 4) | ((lba >> 24) & 0x0F);
    }
    outb(base + ATA_DRIVE_OFFSET, drive_reg);
    
    outb(base + ATA_COMMAND_OFFSET, ATA_CMD_WRITE_SECTORS);
    
    if (ata_wait_drq(drive) != 0) return -1;
    
    const uint16_t *buf16 = (const uint16_t *)buffer;
    for (uint32_t c = 0; c < count; c++) {
        for (int i = 0; i < 256; i++) {
            outw(base + ATA_DATA_OFFSET, *buf16++);
        }
        if (c < count - 1) {
            if (ata_wait_drq(drive) != 0) return -1;
        }
    }
    
    /* Wait for write to complete */
    return ata_wait_ready(drive);
}