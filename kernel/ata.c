/* kernel/ata.c — ATA PIO Driver (A8: Hardened)
 * 
 * Enhanced ATA PIO mode driver with:
 * - Retry logic for transient errors
 * - Better error reporting
 * - Cache flush support
 * - Proper drive detection via IDENTIFY
 * - Timeout handling
 * - 4-drive support (Primary/Secondary Master/Slave)
 */
#include "ata.h"
#include "io.h"
#include "serial.h"
#include "../include/string.h"

/* ATA commands */
#define ATA_CMD_READ_SECTORS     0x20
#define ATA_CMD_READ_SECTORS_EXT 0x24   /* 48-bit LBA */
#define ATA_CMD_WRITE_SECTORS    0x30
#define ATA_CMD_WRITE_SECTORS_EXT 0x34  /* 48-bit LBA */
#define ATA_CMD_CACHE_FLUSH      0xE7
#define ATA_CMD_CACHE_FLUSH_EXT  0xEA   /* 48-bit LBA */
#define ATA_CMD_IDENTIFY         0xEC

/* ATA status bits */
#define ATA_SR_BSY   0x80  /* Busy */
#define ATA_SR_DRDY  0x40  /* Drive ready */
#define ATA_SR_DF    0x20  /* Drive write fault */
#define ATA_SR_DRQ   0x08  /* Data request */
#define ATA_SR_ERR   0x01  /* Error */

/* ATA error bits */
#define ATA_ER_BBK   0x80  /* Bad block */
#define ATA_ER_UNC   0x40  /* Uncorrectable data */
#define ATA_ER_MC    0x20  /* Media changed */
#define ATA_ER_IDNF  0x10  /* ID mark not found */
#define ATA_ER_MCR   0x08  /* Media change request */
#define ATA_ER_ABRT  0x04  /* Command aborted */
#define ATA_ER_TK0NF 0x02  /* Track 0 not found */
#define ATA_ER_AMNF  0x01  /* No address mark */

/* Retry count for transient errors */
#define ATA_MAX_RETRIES 3

/* Timeout values */
#define ATA_TIMEOUT_READY  1000000  /* ~1 second at typical loop speed */
#define ATA_TIMEOUT_DRQ    1000000
#define ATA_TIMEOUT_WRITE  5000000  /* Writes can be slower */

/* Drive information */
typedef struct {
    uint8_t  present;        /* Drive detected */
    uint8_t  is_atapi;       /* ATAPI device (CD-ROM etc.) */
    uint8_t  supports_lba48; /* 48-bit LBA support */
    uint32_t total_sectors;  /* Total sectors (28-bit) */
    uint64_t total_sectors_48; /* Total sectors (48-bit) */
    char model[41];          /* Model string */
    char serial[21];         /* Serial number */
    char firmware[9];        /* Firmware revision */
} ata_drive_info_t;

static ata_drive_info_t ata_drives[4];

/* ====================================================================
 * Low-level helpers
 * ==================================================================== */

static uint16_t ata_get_base(uint8_t drive) {
    return (drive < 2) ? ATA_PRIMARY_BASE : ATA_SECONDARY_BASE;
}

static uint8_t ata_get_slave(uint8_t drive) {
    /* 0=Master, 1=Slave */
    return (drive & 1);
}

/* Read status register */
static uint8_t ata_read_status(uint8_t drive) {
    return inb(ata_get_base(drive) + ATA_STATUS_OFFSET);
}

/* Read error register */
static uint8_t ata_read_error(uint8_t drive) {
    return inb(ata_get_base(drive) + ATA_ERROR_OFFSET);
}

/* 400ns delay (read alternate status 4 times) */
static void ata_delay_400ns(uint8_t drive) {
    /* Reading alternate status register provides ~100ns delay per read */
    uint16_t alt_base = (drive < 2) ? 0x3F6 : 0x376;
    for (int i = 0; i < 4; i++) {
        inb(alt_base);
    }
}

/* Wait for BSY to clear */
static int ata_wait_ready(uint8_t drive) {
    int timeout = ATA_TIMEOUT_READY;
    uint8_t status;
    
    ata_delay_400ns(drive);
    
    do {
        status = ata_read_status(drive);
        if (!(status & ATA_SR_BSY)) {
            return 0;
        }
        timeout--;
    } while (timeout > 0);
    
    serial_write("[ATA] Timeout waiting for drive ready\n");
    return -1;
}

/* Wait for DRQ (data ready) */
static int ata_wait_drq(uint8_t drive) {
    int timeout = ATA_TIMEOUT_DRQ;
    uint8_t status;
    
    do {
        status = ata_read_status(drive);
        if (status & ATA_SR_ERR) {
            uint8_t err = ata_read_error(drive);
            serial_write("[ATA] Error waiting for DRQ: 0x");
            serial_write_hex(err);
            serial_write("\n");
            return -1;
        }
        if (status & ATA_SR_DF) {
            serial_write("[ATA] Drive write fault\n");
            return -1;
        }
        if (status & ATA_SR_DRQ) {
            return 0;
        }
        timeout--;
    } while (timeout > 0);
    
    serial_write("[ATA] Timeout waiting for DRQ\n");
    return -1;
}

/* Wait for write completion */
static int ata_wait_write_complete(uint8_t drive) {
    int timeout = ATA_TIMEOUT_WRITE;
    uint8_t status;
    
    do {
        status = ata_read_status(drive);
        if (status & ATA_SR_ERR) {
            uint8_t err = ata_read_error(drive);
            serial_write("[ATA] Write error: 0x");
            serial_write_hex(err);
            serial_write("\n");
            return -1;
        }
        if (status & ATA_SR_DF) {
            serial_write("[ATA] Drive write fault during completion\n");
            return -1;
        }
        if (!(status & ATA_SR_BSY)) {
            return 0;
        }
        timeout--;
    } while (timeout > 0);
    
    serial_write("[ATA] Timeout waiting for write completion\n");
    return -1;
}

/* Report ATA error register in human-readable form */
static void ata_report_error(uint8_t drive, const char *context) {
    uint8_t err = ata_read_error(drive);
    serial_write("[ATA] Error during ");
    serial_write(context);
    serial_write(": ");
    
    if (err & ATA_ER_BBK)   serial_write("BBK ");
    if (err & ATA_ER_UNC)   serial_write("UNC ");
    if (err & ATA_ER_MC)    serial_write("MC ");
    if (err & ATA_ER_IDNF)  serial_write("IDNF ");
    if (err & ATA_ER_MCR)   serial_write("MCR ");
    if (err & ATA_ER_ABRT)  serial_write("ABRT ");
    if (err & ATA_ER_TK0NF) serial_write("TK0NF ");
    if (err & ATA_ER_AMNF)  serial_write("AMNF ");
    serial_write("\n");
}

/* ====================================================================
 * Drive identification
 * ==================================================================== */

/* Parse IDENTIFY data into drive info */
static void ata_parse_identify(uint8_t drive, uint16_t *identify_data) {
    ata_drive_info_t *info = &ata_drives[drive];
    
    /* Check for ATAPI (word 0 bit 15 clear, word 1 bit 14 set, or word 0 = 0x8580) */
    if (identify_data[0] != 0 && identify_data[0] != 0xFFFF) {
        /* Check bits 14-15 of word 0 */
        if ((identify_data[0] & 0x8000) == 0) {
            info->is_atapi = 0;
        } else {
            info->is_atapi = 1;
        }
    }
    
    /* Check for 48-bit LBA support (word 83, bit 10) */
    if (identify_data[83] & (1 << 10)) {
        info->supports_lba48 = 1;
        /* Total sectors in words 100-103 */
        info->total_sectors_48 = (uint64_t)identify_data[100] |
                                 ((uint64_t)identify_data[101] << 16) |
                                 ((uint64_t)identify_data[102] << 32) |
                                 ((uint64_t)identify_data[103] << 48);
    } else {
        info->supports_lba48 = 0;
        info->total_sectors_48 = 0;
    }
    
    /* 28-bit total sectors (words 60-61) */
    info->total_sectors = (uint32_t)identify_data[60] |
                          ((uint32_t)identify_data[61] << 16);
    
    /* Model string (words 27-46, 40 chars, byte-swapped) */
    for (int i = 0; i < 20; i++) {
        info->model[i * 2] = identify_data[27 + i] >> 8;
        info->model[i * 2 + 1] = identify_data[27 + i] & 0xFF;
    }
    info->model[40] = '\0';
    
    /* Trim trailing spaces */
    for (int i = 39; i >= 0; i--) {
        if (info->model[i] == ' ') {
            info->model[i] = '\0';
        } else {
            break;
        }
    }
    
    /* Serial number (words 10-19, 20 chars, byte-swapped) */
    for (int i = 0; i < 10; i++) {
        info->serial[i * 2] = identify_data[10 + i] >> 8;
        info->serial[i * 2 + 1] = identify_data[10 + i] & 0xFF;
    }
    info->serial[20] = '\0';
    
    /* Firmware revision (words 23-26, 8 chars, byte-swapped) */
    for (int i = 0; i < 4; i++) {
        info->firmware[i * 2] = identify_data[23 + i] >> 8;
        info->firmware[i * 2 + 1] = identify_data[23 + i] & 0xFF;
    }
    info->firmware[8] = '\0';
}

/* Send IDENTIFY command to a drive */
static int ata_identify(uint8_t drive) {
    uint16_t base = ata_get_base(drive);
    uint8_t slave = ata_get_slave(drive);
    
    /* Select drive */
    outb(base + ATA_DRIVE_OFFSET, 0xA0 | (slave << 4));
    ata_delay_400ns(drive);
    
    /* Clear sector count and LBA registers */
    outb(base + ATA_SECCOUNT_OFFSET, 0);
    outb(base + ATA_LBA_LOW_OFFSET, 0);
    outb(base + ATA_LBA_MID_OFFSET, 0);
    outb(base + ATA_LBA_HIGH_OFFSET, 0);
    
    /* Send IDENTIFY command */
    outb(base + ATA_COMMAND_OFFSET, ATA_CMD_IDENTIFY);
    ata_delay_400ns(drive);
    
    /* Check status */
    uint8_t status = ata_read_status(drive);
    if (status == 0) {
        /* Drive doesn't exist */
        return -1;
    }
    
    /* Wait for BSY to clear */
    if (ata_wait_ready(drive) != 0) {
        return -1;
    }
    
    /* Check for ATAPI by looking at LBA_MID and LBA_HIGH */
    uint8_t lba_mid = inb(base + ATA_LBA_MID_OFFSET);
    uint8_t lba_high = inb(base + ATA_LBA_HIGH_OFFSET);
    
    if (lba_mid != 0 || lba_high != 0) {
        /* Not an ATA device (probably ATAPI) */
        if (lba_mid == 0x14 && lba_high == 0xEB) {
            /* ATAPI */
            ata_drives[drive].is_atapi = 1;
            ata_drives[drive].present = 1;
            serial_write("[ATA] Drive ");
            serial_write_dec(drive);
            serial_write(": ATAPI device detected\n");
            return 0;
        }
        /* Unknown device type */
        return -1;
    }
    
    /* Wait for DRQ */
    if (ata_wait_drq(drive) != 0) {
        ata_report_error(drive, "IDENTIFY");
        return -1;
    }
    
    /* Read 256 words of identify data */
    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(base + ATA_DATA_OFFSET);
    }
    
    /* Parse the data */
    ata_parse_identify(drive, identify_data);
    ata_drives[drive].present = 1;
    
    serial_write("[ATA] Drive ");
    serial_write_dec(drive);
    serial_write(": ");
    serial_write(ata_drives[drive].model);
    serial_write("\n");
    serial_write("[ATA]   Serial: ");
    serial_write(ata_drives[drive].serial);
    serial_write(", FW: ");
    serial_write(ata_drives[drive].firmware);
    serial_write("\n");
    serial_write("[ATA]   Sectors (28-bit): ");
    serial_write_dec(ata_drives[drive].total_sectors);
    if (ata_drives[drive].supports_lba48) {
        serial_write("\n[ATA]   Sectors (48-bit): ");
        /* Print 64-bit value */
        serial_write_dec((uint32_t)(ata_drives[drive].total_sectors_48 >> 32));
        serial_write("_");
        serial_write_dec((uint32_t)(ata_drives[drive].total_sectors_48 & 0xFFFFFFFF));
    }
    serial_write("\n");
    if (ata_drives[drive].supports_lba48) {
        serial_write("[ATA]   48-bit LBA: YES\n");
    }
    
    return 0;
}

/* ====================================================================
 * Public API
 * ==================================================================== */

void ata_init(void) {
    serial_write("[ATA] Initializing ATA PIO driver (A8: hardened)...\n");
    
    /* Clear drive info */
    memset(ata_drives, 0, sizeof(ata_drives));
    
    /* Try to identify all 4 drives */
    for (int i = 0; i < 4; i++) {
        if (ata_identify(i) == 0) {
            serial_write("[ATA] Drive ");
            serial_write_dec(i);
            serial_write(": ready\n");
        } else {
            serial_write("[ATA] Drive ");
            serial_write_dec(i);
            serial_write(": not present or not ATA\n");
        }
    }
    
    serial_write("[ATA] Initialization complete.\n");
}

/* ====================================================================
 * Read sector with retry logic
 * ==================================================================== */

int ata_read_sector(uint8_t drive, uint32_t lba, uint8_t *buffer, uint32_t count) {
    if (!buffer || count == 0 || drive > 3) return -1;
    if (!ata_drives[drive].present) {
        serial_write("[ATA] Drive ");
        serial_write_dec(drive);
        serial_write(" not present\n");
        return -1;
    }
    
    uint16_t base = ata_get_base(drive);
    uint8_t slave = ata_get_slave(drive);
    
    for (int retry = 0; retry < ATA_MAX_RETRIES; retry++) {
        if (retry > 0) {
            serial_write("[ATA] Retry ");
            serial_write_dec(retry);
            serial_write(" for read LBA=");
            serial_write_hex(lba);
            serial_write("\n");
        }
        
        /* Wait for drive ready */
        if (ata_wait_ready(drive) != 0) {
            continue;
        }
        
        /* Set up LBA and count */
        outb(base + ATA_SECCOUNT_OFFSET, count & 0xFF);
        outb(base + ATA_LBA_LOW_OFFSET, lba & 0xFF);
        outb(base + ATA_LBA_MID_OFFSET, (lba >> 8) & 0xFF);
        outb(base + ATA_LBA_HIGH_OFFSET, (lba >> 16) & 0xFF);
        
        /* Select drive and set LBA mode */
        uint8_t drive_reg = 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F);
        outb(base + ATA_DRIVE_OFFSET, drive_reg);
        ata_delay_400ns(drive);
        
        /* Send read command */
        outb(base + ATA_COMMAND_OFFSET, ATA_CMD_READ_SECTORS);
        
        /* Wait for data */
        if (ata_wait_drq(drive) != 0) {
            ata_report_error(drive, "read");
            continue; /* Retry */
        }
        
        /* Read data */
        uint16_t *buf16 = (uint16_t *)buffer;
        for (uint32_t c = 0; c < count; c++) {
            for (int i = 0; i < 256; i++) {
                *buf16++ = inw(base + ATA_DATA_OFFSET);
            }
            /* Wait for next sector if count > 1 */
            if (c < count - 1) {
                if (ata_wait_drq(drive) != 0) {
                    ata_report_error(drive, "multi-sector read");
                    goto retry_read;
                }
            }
        }
        
        return 0; /* Success */
        
    retry_read:
        continue;
    }
    
    serial_write("[ATA] Read failed after ");
    serial_write_dec(ATA_MAX_RETRIES);
    serial_write(" retries at LBA=");
    serial_write_hex(lba);
    serial_write("\n");
    return -1;
}

/* ====================================================================
 * Write sector with retry logic
 * ==================================================================== */

int ata_write_sector(uint8_t drive, uint32_t lba, const uint8_t *buffer, uint32_t count) {
    if (!buffer || count == 0 || drive > 3) return -1;
    if (!ata_drives[drive].present) {
        serial_write("[ATA] Drive ");
        serial_write_dec(drive);
        serial_write(" not present\n");
        return -1;
    }
    
    uint16_t base = ata_get_base(drive);
    uint8_t slave = ata_get_slave(drive);
    
    for (int retry = 0; retry < ATA_MAX_RETRIES; retry++) {
        if (retry > 0) {
            serial_write("[ATA] Retry ");
            serial_write_dec(retry);
            serial_write(" for write LBA=");
            serial_write_hex(lba);
            serial_write("\n");
        }
        
        /* Wait for drive ready */
        if (ata_wait_ready(drive) != 0) {
            continue;
        }
        
        /* Set up LBA and count */
        outb(base + ATA_SECCOUNT_OFFSET, count & 0xFF);
        outb(base + ATA_LBA_LOW_OFFSET, lba & 0xFF);
        outb(base + ATA_LBA_MID_OFFSET, (lba >> 8) & 0xFF);
        outb(base + ATA_LBA_HIGH_OFFSET, (lba >> 16) & 0xFF);
        
        /* Select drive */
        uint8_t drive_reg = 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F);
        outb(base + ATA_DRIVE_OFFSET, drive_reg);
        ata_delay_400ns(drive);
        
        /* Send write command */
        outb(base + ATA_COMMAND_OFFSET, ATA_CMD_WRITE_SECTORS);
        
        /* Wait for DRQ */
        if (ata_wait_drq(drive) != 0) {
            ata_report_error(drive, "write");
            continue;
        }
        
        /* Write data */
        const uint16_t *buf16 = (const uint16_t *)buffer;
        for (uint32_t c = 0; c < count; c++) {
            for (int i = 0; i < 256; i++) {
                outw(base + ATA_DATA_OFFSET, *buf16++);
            }
            if (c < count - 1) {
                if (ata_wait_drq(drive) != 0) {
                    ata_report_error(drive, "multi-sector write");
                    goto retry_write;
                }
            }
        }
        
        /* Wait for write to complete */
        if (ata_wait_write_complete(drive) != 0) {
            continue;
        }
        
        return 0; /* Success */
        
    retry_write:
        continue;
    }
    
    serial_write("[ATA] Write failed after ");
    serial_write_dec(ATA_MAX_RETRIES);
    serial_write(" retries at LBA=");
    serial_write_hex(lba);
    serial_write("\n");
    return -1;
}

/* ====================================================================
 * Cache flush (important for data integrity)
 * ==================================================================== */

int ata_flush_cache(uint8_t drive) {
    if (drive > 3 || !ata_drives[drive].present) return -1;
    
    uint16_t base = ata_get_base(drive);
    uint8_t slave = ata_get_slave(drive);
    
    /* Select drive */
    outb(base + ATA_DRIVE_OFFSET, 0xE0 | (slave << 4));
    ata_delay_400ns(drive);
    
    /* Send cache flush command */
    outb(base + ATA_COMMAND_OFFSET, ATA_CMD_CACHE_FLUSH);
    
    /* Wait for completion */
    if (ata_wait_ready(drive) != 0) {
        serial_write("[ATA] Cache flush timeout\n");
        return -1;
    }
    
    uint8_t status = ata_read_status(drive);
    if (status & ATA_SR_ERR) {
        ata_report_error(drive, "cache flush");
        return -1;
    }
    
    return 0;
}

/* ====================================================================
 * Drive info query
 * ==================================================================== */

int ata_is_present(uint8_t drive) {
    if (drive > 3) return 0;
    return ata_drives[drive].present;
}

uint32_t ata_get_total_sectors(uint8_t drive) {
    if (drive > 3 || !ata_drives[drive].present) return 0;
    return ata_drives[drive].total_sectors;
}

int ata_is_atapi(uint8_t drive) {
    if (drive > 3) return 0;
    return ata_drives[drive].is_atapi;
}
