#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include <stddef.h>

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID_82540EM 0x100E

/* MMIO Registers (offsets from BAR0) */
#define E1000_REG_CTRL      0x0000  /* Device Control */
#define E1000_REG_STATUS    0x0008  /* Device Status */
#define E1000_REG_EERD      0x0014  /* EEPROM Read */
#define E1000_REG_CTRL_EXT  0x0018  /* Extended Device Control */
#define E1000_REG_ICR       0x00C0  /* Interrupt Cause Read */
#define E1000_REG_IMS       0x00D0  /* Interrupt Mask Set */
#define E1000_REG_RCTL      0x0100  /* RX Control */
#define E1000_REG_TCTL      0x0400  /* TX Control */
#define E1000_REG_TIPG      0x0410  /* TX Inter Packet Gap */
#define E1000_REG_RDBAL     0x2800  /* RX Descriptor Base Address Low */
#define E1000_REG_RDBAH     0x2804  /* RX Descriptor Base Address High */
#define E1000_REG_RDLEN     0x2808  /* RX Descriptor Length */
#define E1000_REG_RDH       0x2810  /* RX Descriptor Head */
#define E1000_REG_RDT       0x2818  /* RX Descriptor Tail */
#define E1000_REG_TDBAL     0x3800  /* TX Descriptor Base Address Low */
#define E1000_REG_TDBAH     0x3804  /* TX Descriptor Base Address High */
#define E1000_REG_TDLEN     0x3808  /* TX Descriptor Length */
#define E1000_REG_TDH       0x3810  /* TX Descriptor Head */
#define E1000_REG_TDT       0x3818  /* TX Descriptor Tail */
#define E1000_REG_MTA       0x5200  /* Multicast Table Array */
#define E1000_REG_RAL       0x5400  /* Receive Address Low */
#define E1000_REG_RAH       0x5404  /* Receive Address High */

/* CTRL Register Bits */
#define E1000_CTRL_RST      (1 << 26) /* Device Reset */
#define E1000_CTRL_SLU      (1 << 6)  /* Set Link Up */

/* STATUS Register Bits */
#define E1000_STATUS_LU     (1 << 1)  /* Link Up */

/* RCTL Register Bits */
#define E1000_RCTL_EN       (1 << 1)  /* Receiver Enable */
#define E1000_RCTL_BAM      (1 << 15) /* Broadcast Accept Mode */
#define E1000_RCTL_SZ_2048  (3 << 16) /* Receive Buffer Size = 2048 */
#define E1000_RCTL_SECRC    (1 << 26) /* Strip Ethernet CRC */

/* TCTL Register Bits */
#define E1000_TCTL_EN       (1 << 1)  /* Transmit Enable */
#define E1000_TCTL_PSP      (1 << 3)  /* Pad Short Packets */

/* Descriptor Command Bits */
#define E1000_TXD_CMD_EOP   (1 << 0)  /* End of Packet */
#define E1000_TXD_CMD_IFCS  (1 << 1)  /* Insert FCS */
#define E1000_TXD_CMD_RS    (1 << 3)  /* Report Status */

/* RX Descriptor Status Bits */
#define E1000_RXD_STAT_DD   (1 << 0)  /* Descriptor Done */

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

typedef struct {
    uint8_t mac[6];
    uint8_t *mmio_base;
    e1000_rx_desc_t *rx_ring;
    uint64_t rx_ring_phys;
    uint8_t **rx_buffers;
    uint64_t *rx_buffer_phys;
    uint16_t rx_ring_size;
    
    e1000_tx_desc_t *tx_ring;
    uint64_t tx_ring_phys;
    uint8_t **tx_buffers;
    uint64_t *tx_buffer_phys;
    uint16_t tx_ring_size;
    uint16_t tx_cur;
} e1000_dev_t;

extern e1000_dev_t e1000_dev;

void e1000_init(uint8_t bus, uint8_t slot, uint8_t func);
void e1000_read_mac(void);
int e1000_transmit(uint8_t *data, uint16_t len);

#endif // E1000_H
