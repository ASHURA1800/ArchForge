#include <pci.h>
#include <io.h>
#include <serial.h>
#include <serial.h>
#include <stdint.h>

uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

void pci_scan(void) {
    serial_write("[PCI] Scanning for devices...\n");
    for (uint8_t bus = 0; bus < 8; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t header = pci_read_config_dword(bus, slot, 0, 0);
            if ((header & 0xFFFF) == 0xFFFF) {
                continue; // No device
            }
            
            uint16_t vendor_id = header & 0xFFFF;
            uint16_t device_id = (header >> 16) & 0xFFFF;
            uint8_t header_type = (pci_read_config_dword(bus, slot, 0, PCI_HEADER_TYPE) >> 16) & 0xFF;
            
            serial_write("[PCI] Found device: Bus ");
            serial_write_hex(bus);
            serial_write(", Slot ");
            serial_write_hex(slot);
            serial_write(", Vendor 0x");
            serial_write_hex(vendor_id);
            serial_write(", Device 0x");
            serial_write_hex(device_id);
            serial_write("\n");
            
            if (header_type == PCI_HEADER_TYPE_NORMAL) {
                uint32_t bar0 = pci_read_config_dword(bus, slot, 0, PCI_BAR0);
                uint32_t bar1 = pci_read_config_dword(bus, slot, 0, PCI_BAR1);
                uint8_t irq_line = (pci_read_config_dword(bus, slot, 0, PCI_INTERRUPT_LINE) >> 16) & 0xFF;
                
                serial_write("  BAR0: 0x");
                serial_write_hex(bar0);
                serial_write(", BAR1: 0x");
                serial_write_hex(bar1);
                serial_write(", IRQ: ");
                serial_write_hex(irq_line);
                serial_write("\n");
                
                // Check for e1000
                if (vendor_id == 0x8086 && (device_id == 0x100E || device_id == 0x100F || device_id == 0x10D3 || device_id == 0x15B8)) {
                    serial_write("[PCI] *** Intel e1000 NIC detected! ***\n");
                    
                    // Enable Bus Mastering and Memory Space
                    uint32_t cmd = pci_read_config_dword(bus, slot, 0, PCI_COMMAND);
                    cmd |= PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER | PCI_COMMAND_IO_SPACE;
                    pci_write_config_dword(bus, slot, 0, PCI_COMMAND, cmd);
                    serial_write("[PCI] Enabled Memory Space, IO Space, and Bus Mastering for e1000.\n");
                }
            }
        }
    }
    serial_write("[PCI] Scan complete.\n");
}