#include "e1000.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include "io.h"
#include "string.h"
#include "heap.h"

extern uint64_t hhdm_offset;

/* E1000 MMIO virtual address - must be outside HHDM and heap regions
 * HHDM: 0xffff800000000000+
 * Heap: 0xffffa00000000000 (16MB)
 * E1000 MMIO: 0xffffc00000000000 (safe gap)
 */
#define E1000_MMIO_VA 0xFFFFC00000000000ULL

e1000_dev_t e1000_dev;

static inline uint32_t e1000_read_reg(uint32_t reg) {
    return *(volatile uint32_t *)(e1000_dev.mmio_base + reg);
}

static inline void e1000_write_reg(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(e1000_dev.mmio_base + reg) = val;
}

void e1000_init(uint8_t bus, uint8_t slot, uint8_t func) {
    serial_write("[E1000] Initializing...\n");

    uint32_t bar0 = pci_read_config_dword(bus, slot, func, PCI_BAR0);
    uint64_t phys_addr = bar0 & ~0xF;

    serial_write("[E1000] BAR0 physical address: 0x");
    serial_write_hex(phys_addr);
    serial_write("\n");

    if (vmm_map_mmio(E1000_MMIO_VA, phys_addr, 0x20000, 0x03) != 0) {
        serial_write("[E1000] Failed to map MMIO!\n");
        return;
    }
    e1000_dev.mmio_base = (uint8_t *)E1000_MMIO_VA;
    serial_write("[E1000] MMIO mapped at virtual address: 0x");
    serial_write_hex((uint64_t)e1000_dev.mmio_base);
    serial_write("\n");

    serial_write("[E1000] Resetting device...\n");
    e1000_write_reg(E1000_REG_CTRL, e1000_read_reg(E1000_REG_CTRL) | E1000_CTRL_RST);

    int timeout = 100000;
    while (e1000_read_reg(E1000_REG_CTRL) & E1000_CTRL_RST) {
        if (--timeout == 0) {
            serial_write("[E1000] Reset timeout!\n");
            return;
        }
    }
    serial_write("[E1000] Reset complete.\n");

    uint32_t ralt = e1000_read_reg(E1000_REG_RAL);
    uint32_t raht = e1000_read_reg(E1000_REG_RAH);
    e1000_dev.mac[0] = ralt & 0xFF;
    e1000_dev.mac[1] = (ralt >> 8) & 0xFF;
    e1000_dev.mac[2] = (ralt >> 16) & 0xFF;
    e1000_dev.mac[3] = (ralt >> 24) & 0xFF;
    e1000_dev.mac[4] = raht & 0xFF;
    e1000_dev.mac[5] = (raht >> 8) & 0xFF;

    serial_write("[E1000] MAC Address: ");
    serial_write_hex(e1000_dev.mac[0]);
    serial_write(":");
    serial_write_hex(e1000_dev.mac[1]);
    serial_write(":");
    serial_write_hex(e1000_dev.mac[2]);
    serial_write(":");
    serial_write_hex(e1000_dev.mac[3]);
    serial_write(":");
    serial_write_hex(e1000_dev.mac[4]);
    serial_write(":");
    serial_write_hex(e1000_dev.mac[5]);
    serial_write("\n");

    // Program our MAC into RAL/RAH (Receive Address Register 0)
    uint32_t ral = 0;
    uint32_t rah = 0;
    for (int i = 0; i < 6; i++) {
        if (i < 4) {
            ral |= ((uint32_t)e1000_dev.mac[i] << (i * 8));
        } else {
            rah |= ((uint32_t)e1000_dev.mac[i] << ((i - 4) * 8));
        }
    }
    rah |= E1000_RAH_AV; // Address Valid bit
    e1000_write_reg(E1000_REG_RAL, ral);
    e1000_write_reg(E1000_REG_RAH, rah);
    serial_write("[E1000] Programmed RAL/RAH with our MAC\n");
    
    // Read back to verify
    uint32_t ral_verify = e1000_read_reg(E1000_REG_RAL);
    uint32_t rah_verify = e1000_read_reg(E1000_REG_RAH);
    serial_write("[E1000] RAL verify: 0x");
    serial_write_hex(ral_verify);
    serial_write(" RAH verify: 0x");
    serial_write_hex(rah_verify);
    serial_write("\n");
    
    // Also program gateway MAC in RAL1/RAH1
    uint8_t gateway_mac[6] = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x02};
    uint32_t ral1 = 0;
    uint32_t rah1 = 0;
    for (int i = 0; i < 6; i++) {
        if (i < 4) {
            ral1 |= ((uint32_t)gateway_mac[i] << (i * 8));
        } else {
            rah1 |= ((uint32_t)gateway_mac[i] << ((i - 4) * 8));
        }
    }
    rah1 |= E1000_RAH_AV; // Address Valid bit
    e1000_write_reg(E1000_REG_RAL + 8, ral1);
    e1000_write_reg(E1000_REG_RAH + 8, rah1);
    serial_write("[E1000] Programmed RAL1/RAH1 with gateway MAC\n");
    
    // Clear other receive address registers (2-15) to disable their filters (0 and 1 are used)
    for (int i = 2; i < 16; i++) {
        e1000_write_reg(E1000_REG_RAL + i * 8, 0);
        e1000_write_reg(E1000_REG_RAH + i * 8, 0);
    }
    serial_write("[E1000] Cleared other RAL/RAH registers\n");

    // 1. Allocate RX ring (32 descriptors) - 32 * 16 = 512 bytes = 1 page
    e1000_dev.rx_ring_size = 32;
    e1000_dev.rx_ring_phys = (uint64_t)pmm_alloc(1);
    if (!e1000_dev.rx_ring_phys) {
        serial_write("[E1000] Failed to allocate RX ring!\n");
        return;
    }
    e1000_dev.rx_ring = (e1000_rx_desc_t *)(e1000_dev.rx_ring_phys + hhdm_offset);
    memset(e1000_dev.rx_ring, 0, e1000_dev.rx_ring_size * sizeof(e1000_rx_desc_t));

    // Allocate RX buffers (2048 bytes each, one page each)
    e1000_dev.rx_buffers = (uint8_t **)kmalloc(e1000_dev.rx_ring_size * sizeof(uint8_t *));
    e1000_dev.rx_buffer_phys = (uint64_t *)kmalloc(e1000_dev.rx_ring_size * sizeof(uint64_t));
    for (int i = 0; i < e1000_dev.rx_ring_size; i++) {
        e1000_dev.rx_buffer_phys[i] = (uint64_t)pmm_alloc(1); // 1 page = 4096 bytes
        e1000_dev.rx_buffers[i] = (uint8_t *)(e1000_dev.rx_buffer_phys[i] + hhdm_offset);
        e1000_dev.rx_ring[i].addr = e1000_dev.rx_buffer_phys[i];
        e1000_dev.rx_ring[i].length = 0;
        e1000_dev.rx_ring[i].status = 0;
    }

    // Enable RX with proper settings - follow Intel 82540EM datasheet order
            // 1. Set RX descriptor base address and length first
            e1000_write_reg(E1000_REG_RDBAL, (uint32_t)(e1000_dev.rx_ring_phys & 0xFFFFFFFF));
            e1000_write_reg(E1000_REG_RDBAH, 0);
            e1000_write_reg(E1000_REG_RDLEN, e1000_dev.rx_ring_size * sizeof(e1000_rx_desc_t));
            e1000_write_reg(E1000_REG_RDH, 0);
            e1000_write_reg(E1000_REG_RDT, e1000_dev.rx_ring_size - 1);
   
            // 2. Read back to verify ring setup
            uint32_t rdbal = e1000_read_reg(E1000_REG_RDBAL);
            uint32_t rdlen = e1000_read_reg(E1000_REG_RDLEN);
            uint32_t rdh = e1000_read_reg(E1000_REG_RDH);
            uint32_t rdt = e1000_read_reg(E1000_REG_RDT);
            serial_write("[E1000] RX Ring: RDBA=0x");
            serial_write_hex(rdbal);
            serial_write(" RDLEN=");
            serial_write_dec(rdlen);
            serial_write(" RDH=");
            serial_write_dec(rdh);
            serial_write(" RDT=");
            serial_write_dec(rdt);
            serial_write("\n");
   
            // 3. Configure RCTL with all needed bits
            // Clear size bits (16-17) first, then set to 2048 (00)
            uint32_t rctl = e1000_read_reg(E1000_REG_RCTL);
            serial_write("[E1000] RCTL before: 0x");
            serial_write_hex(rctl);
            serial_write("\n");
            rctl &= ~(3 << 16);
            /* Clear bit 30 (Extended Receive Descriptor) - 82540EM only supports legacy 16-byte descriptors */
                    rctl &= ~E1000_RCTL_EXT;
            rctl |= E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SZ_2048 | E1000_RCTL_SECRC | E1000_RCTL_UPE;
            // Also enable MPE (Multicast Promiscuous Enable), VPE (VLAN Promiscuous Enable) and SBP (Store Bad Packets)
            rctl |= (1 << 4) | (1 << 2) | (1 << 1);  // MPE | SBP | VPE
            // Enable UPE (Unicast Promiscuous Enable) - bit 3, critical for receiving packets not matching our MAC
            rctl |= E1000_RCTL_UPE;
            // Enable Long Packet Enable (LPE) - bit 5, for jumbo frames
            rctl |= (1 << 5);
            e1000_write_reg(E1000_REG_RCTL, rctl);
            serial_write("[E1000] RCTL written: 0x");
            serial_write_hex(rctl);
            serial_write("\n");
   
            // 4. Wait for RCTL.EN to take effect
            for (int i = 0; i < 10000; i++) {
                uint32_t rctl_check = e1000_read_reg(E1000_REG_RCTL);
                if (rctl_check & E1000_RCTL_EN) break;
                __asm__ volatile("pause");
            }
    
    // 5. Read first RX descriptor to verify
    serial_write("[E1000] First RX desc after RCTL: addr=0x");
    serial_write_hex(e1000_dev.rx_ring[0].addr);
    serial_write(" status=0x");
    serial_write_hex(e1000_dev.rx_ring[0].status);
    serial_write("\n");
    
    // Verify STATUS register
    uint32_t status = e1000_read_reg(E1000_REG_STATUS);
    serial_write("[E1000] STATUS = 0x");
    serial_write_hex(status);
    serial_write(" (LU=");
    serial_write((status & E1000_STATUS_LU) ? "1" : "0");
    serial_write(")\n");
    
    // Read first RX descriptor to verify
    serial_write("[E1000] First RX desc: addr=0x");
    serial_write_hex(e1000_dev.rx_ring[0].addr);
    serial_write(" status=0x");
    serial_write_hex(e1000_dev.rx_ring[0].status);
    serial_write("\n");

    // 2. Allocate TX ring (32 descriptors) - 32 * 16 = 512 bytes = 1 page
    e1000_dev.tx_ring_size = 32;
    e1000_dev.tx_ring_phys = (uint64_t)pmm_alloc(1);
    if (!e1000_dev.tx_ring_phys) {
        serial_write("[E1000] Failed to allocate TX ring!\n");
        return;
    }
    e1000_dev.tx_ring = (e1000_tx_desc_t *)(e1000_dev.tx_ring_phys + hhdm_offset);
    memset(e1000_dev.tx_ring, 0, e1000_dev.tx_ring_size * sizeof(e1000_tx_desc_t));

    e1000_dev.tx_buffers = (uint8_t **)kmalloc(e1000_dev.tx_ring_size * sizeof(uint8_t *));
    e1000_dev.tx_buffer_phys = (uint64_t *)kmalloc(e1000_dev.tx_ring_size * sizeof(uint64_t));
    for (int i = 0; i < e1000_dev.tx_ring_size; i++) {
        e1000_dev.tx_buffer_phys[i] = (uint64_t)pmm_alloc(1);
        e1000_dev.tx_buffers[i] = (uint8_t *)(e1000_dev.tx_buffer_phys[i] + hhdm_offset);
        e1000_dev.tx_ring[i].addr = e1000_dev.tx_buffer_phys[i];
        e1000_dev.tx_ring[i].length = 0;
        e1000_dev.tx_ring[i].cmd = 0;
        e1000_dev.tx_ring[i].status = 0x01;  // DD bit set = descriptor is free
    }
    e1000_dev.tx_cur = 0;

    // Setup TX ring registers
    e1000_write_reg(E1000_REG_TDBAL, (uint32_t)(e1000_dev.tx_ring_phys & 0xFFFFFFFF));
    e1000_write_reg(E1000_REG_TDBAH, 0);
    e1000_write_reg(E1000_REG_TDLEN, e1000_dev.tx_ring_size * sizeof(e1000_tx_desc_t));
    e1000_write_reg(E1000_REG_TDH, 0);
    e1000_write_reg(E1000_REG_TDT, 0);

    // Enable TX with proper settings
    uint32_t tctl = e1000_read_reg(E1000_REG_TCTL);
    tctl |= E1000_TCTL_EN | E1000_TCTL_PSP | (0x0F << 4) | (0x40 << 12); // CT = 0x0F, COLD = 0x40
    e1000_write_reg(E1000_REG_TCTL, tctl);

    // Set TIPG (inter-packet gap)
    e1000_write_reg(E1000_REG_TIPG, 0x0060200A);

    // Clear all 128 MTA entries
    for (int i = 0; i < 128; i++) {
        e1000_write_reg(E1000_REG_MTA + i * 4, 0);
    }

    // Mask all interrupts (polling mode)
    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFF);
    e1000_read_reg(E1000_REG_ICR); // Clear ICR

    // Set Link Up
    uint32_t ctrl = e1000_read_reg(E1000_REG_CTRL);
    ctrl |= E1000_CTRL_SLU;
    e1000_write_reg(E1000_REG_CTRL, ctrl);

    // Poll STATUS.LU with timeout
    timeout = 100000;
    while (timeout > 0) {
        uint32_t status = e1000_read_reg(E1000_REG_STATUS);
        if (status & E1000_STATUS_LU) {
            serial_write("[E1000] Link up detected (STATUS.LU=1)\n");
            break;
        }
        if (--timeout == 0) {
            serial_write("[E1000] WARNING: Link up timeout (STATUS.LU=0)\n");
            break;
        }
    }

    // Initialize software RX cursor
    e1000_dev.rx_cur = 0;

    serial_write("[E1000] RX/TX rings initialized and link up.\n");
}

int e1000_transmit(uint8_t *data, uint16_t len) {
    if (len > 2048) return -1;

    uint16_t cur = e1000_dev.tx_cur;

    // Wait for descriptor to be free (DD bit = 1 means done)
    int timeout = 100000;
    while ((*(volatile uint8_t *)&e1000_dev.tx_ring[cur].status & 0x01) == 0) {
        if (--timeout == 0) {
            serial_write("[E1000] TX timeout!\n");
            return -1;
        }
    }

    // Copy data to buffer
    memcpy(e1000_dev.tx_buffers[cur], data, len);

    // Setup descriptor
    e1000_dev.tx_ring[cur].length = len;
    e1000_dev.tx_ring[cur].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    e1000_dev.tx_ring[cur].status = 0;

    // Compiler barrier to ensure descriptor is visible before tail update
    asm volatile("" ::: "memory");

    // Advance TDT
    e1000_dev.tx_cur = (cur + 1) % e1000_dev.tx_ring_size;
    e1000_write_reg(E1000_REG_TDT, e1000_dev.tx_cur);

    // Wait for DD bit to be set (transmission complete)
    timeout = 100000;
    while ((*(volatile uint8_t *)&e1000_dev.tx_ring[cur].status & 0x01) == 0) {
        if (--timeout == 0) {
            serial_write("[E1000] TX completion timeout!\n");
            return -1;
        }
    }
    serial_write("[E1000] TX completed (DD=1)\n");

    return 0;
}

int e1000_poll_rx(void) {
    int count = 0;
    
    while (1) {
        volatile e1000_rx_desc_t *desc = &e1000_dev.rx_ring[e1000_dev.rx_cur];
        
        // Compiler barrier before reading descriptor
        asm volatile("" ::: "memory");
        
        uint8_t status = desc->status;
        
        serial_write("[E1000] Poll RX: rx_cur=");
        serial_write_dec(e1000_dev.rx_cur);
        serial_write(" status=0x");
        serial_write_hex(status);
        
        // Also read RDH/RDT for debugging
        uint16_t rdh = e1000_read_reg(E1000_REG_RDH);
        uint16_t rdt = e1000_read_reg(E1000_REG_RDT);
        serial_write(" RDH=");
        serial_write_dec(rdh);
        serial_write(" RDT=");
        serial_write_dec(rdt);
        
        // Read the descriptor's buffer address
        uint64_t addr = desc->addr;
        serial_write(" buf=0x");
        serial_write_hex(addr);
        
        // Also read RCTL and STATUS for debugging
        uint32_t rctl = e1000_read_reg(E1000_REG_RCTL);
        uint32_t status_reg = e1000_read_reg(E1000_REG_STATUS);
        serial_write(" RCTL=0x");
        serial_write_hex(rctl);
                         serial_write(" STATUS=0x");
                         serial_write_hex(status_reg);
                         serial_write("\n");
                         
                         // Read RX counter
                         uint32_t rxc = e1000_read_reg(E1000_REG_RXC);
                         serial_write("[E1000] RXC=0x");
                         serial_write_hex(rxc);
                         serial_write("\n");
        
        if ((status & E1000_RXD_STAT_DD) == 0) {
            break; // No more completed packets
        }
        
        uint16_t len = desc->length;
        serial_write("[E1000] RX packet received, length: ");
        serial_write_dec(len);
        serial_write("\n");
        
        // Dump first 16 bytes for diagnostics
        serial_write("[E1000] RX frame first 16 bytes: ");
        for (int i = 0; i < 16 && i < len; i++) {
            serial_write_hex(e1000_dev.rx_buffers[e1000_dev.rx_cur][i]);
            serial_write(" ");
        }
        serial_write("\n");
        
        // Check if it's an ARP reply
        if (len >= 28) {
            uint8_t *frame = e1000_dev.rx_buffers[e1000_dev.rx_cur];
            uint16_t ethertype = *(uint16_t *)(frame + 12);
            if (ethertype == 0x0806) { // ARP
                uint16_t opcode = *(uint16_t *)(frame + 20);
                if (opcode == 0x0200) { // ARP reply (little endian)
                    serial_write("[E1000] ARP reply received!\n");
                    // Check sender IP is 10.0.2.2
                    uint32_t sender_ip = *(uint32_t *)(frame + 28);
                    if (sender_ip == 0x0202000A) { // 10.0.2.2
                        serial_write("[E1000] ARP reply from 10.0.2.2 confirmed!\n");
                    }
                }
            }
        }
        
        // Reset descriptor
        desc->status = 0;
        
        // Write RDT to advance hardware tail
        e1000_write_reg(E1000_REG_RDT, e1000_dev.rx_cur);
        
        // Advance software cursor
        e1000_dev.rx_cur = (e1000_dev.rx_cur + 1) % e1000_dev.rx_ring_size;
        
        count++;
    }
    
    if (count > 0) {
        serial_write("[E1000] Poll RX: processed ");
        serial_write_dec(count);
        serial_write(" packets\n");
    }
    return count;
}