#include <e1000.h>
#include <pci.h>
#include <vmm.h>
#include <pmm.h>
#include <serial.h>
#include <io.h>

e1000_dev_t e1000_dev;

static inline uint32_t e1000_read_reg(uint32_t reg) {
    return *(volatile uint32_t *)(e1000_dev.mmio_base + reg);
}

static inline void e1000_write_reg(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(e1000_dev.mmio_base + reg) = val;
}

void e1000_init(uint8_t bus, uint8_t slot, uint8_t func) {
    (void)bus; (void)slot; (void)func;
    serial_write("[E1000] Initializing...\n");
    
    uint32_t bar0 = pci_read_config_dword(bus, slot, func, PCI_BAR0);
    uint64_t phys_addr = bar0 & ~0xF;
    
    serial_write("[E1000] BAR0 physical address: 0x");
    serial_write_hex(phys_addr);
    serial_write("\n");
    
    uint64_t virt_addr = 0xFFFF800000000000ULL;
    if (vmm_map_mmio(virt_addr, phys_addr, 0x20000, 0x03) != 0) {
        serial_write("[E1000] Failed to map MMIO!\n");
        return;
    }
    e1000_dev.mmio_base = (uint8_t *)virt_addr;
    serial_write("[E1000] MMIO mapped at virtual address: 0x");
    serial_write_hex((uint64_t)e1000_dev.mmio_base);
    serial_write("\n");
    
    serial_write("[E1000] Resetting device...\n");
    e1000_write_reg(0x0000, e1000_read_reg(0x0000) | (1 << 26));
    
    int timeout = 100000;
    while (e1000_read_reg(0x0000) & (1 << 26)) {
        if (--timeout == 0) {
            serial_write("[E1000] Reset timeout!\n");
            return;
        }
    }
    serial_write("[E1000] Reset complete.\n");
    
    uint32_t ralt = e1000_read_reg(0x5400);
    uint32_t raht = e1000_read_reg(0x5404);
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
    
    serial_write("[E1000] Initialization complete.\n");
}
