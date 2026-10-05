#include "serial.h"
#include "string.h"
#include "e1000.h"
#include "pci.h"

// Simple ARP request packet structure
typedef struct {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype; // 0x0806 for ARP
    uint16_t hw_type;   // 1 for Ethernet
    uint16_t proto_type; // 0x0800 for IPv4
    uint8_t hw_size;    // 6
    uint8_t proto_size; // 4
    uint16_t opcode;    // 1 for request
    uint8_t sender_mac[6];
    uint32_t sender_ip; // 10.0.2.15 = 0x0F02000A (little endian)
    uint8_t target_mac[6];
    uint32_t target_ip; // 10.0.2.2 = 0x0202000A (little endian)
} __attribute__((packed)) arp_packet_t;

void net_selftest_n3(void) {
    serial_write("[NET_SELFTEST] Starting N3 e1000 polling test...\n");
    
    // Find e1000
    int found = 0;
    for (uint8_t bus = 0; bus < 8; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t header = pci_read_config_dword(bus, slot, 0, 0);
            if ((header & 0xFFFF) == 0xFFFF) continue;
            
            uint16_t vendor_id = header & 0xFFFF;
            uint16_t device_id = (header >> 16) & 0xFFFF;
            
            if (vendor_id == 0x8086 && (device_id == 0x100E || device_id == 0x100F || device_id == 0x10D3 || device_id == 0x15B8)) {
                serial_write("[NET_SELFTEST] Found e1000 at bus ");
                serial_write_dec(bus);
                serial_write(" slot ");
                serial_write_dec(slot);
                serial_write("\n");
                
                e1000_init(bus, slot, 0);
                found = 1;
                break;
            }
        }
        if (found) break;
    }
    
    if (!found) {
        serial_write("[NET_SELFTEST] FAIL: e1000 not found!\n");
        return;
    }
    
    // Build broadcast ARP request
    arp_packet_t arp;
    memset(&arp, 0, sizeof(arp));
    
    // Broadcast MAC
    arp.dst_mac[0] = 0xFF; arp.dst_mac[1] = 0xFF; arp.dst_mac[2] = 0xFF;
    arp.dst_mac[3] = 0xFF; arp.dst_mac[4] = 0xFF; arp.dst_mac[5] = 0xFF;
    
    // Source MAC (copy from e1000)
    memcpy(arp.src_mac, e1000_dev.mac, 6);
    
    arp.ethertype = 0x0608; // 0x0806 in little endian
    arp.hw_type = 0x0100;   // 1 in little endian
    arp.proto_type = 0x0008; // 0x0800 in little endian
    arp.hw_size = 6;
    arp.proto_size = 4;
    arp.opcode = 0x0100;    // 1 in little endian
    
    memcpy(arp.sender_mac, e1000_dev.mac, 6);
    arp.sender_ip = 0x0F02000A; // 10.0.2.15
    
    // Target MAC is 00:00:00:00:00:00
    arp.target_ip = 0x0202000A; // 10.0.2.2
    
    serial_write("[NET_SELFTEST] Transmitting ARP request...\n");
    if (e1000_transmit((uint8_t *)&arp, sizeof(arp)) != 0) {
        serial_write("[NET_SELFTEST] FAIL: e1000_transmit returned error!\n");
        return;
    }
    
    serial_write("[NET_SELFTEST] ARP request transmitted. Polling for reply...\n");
    
    // Longer delay to let RX initialize (hardware needs time after RCTL.EN)
    for (volatile int i = 0; i < 5000000; i++);
    
    // Poll for more iterations
    int rx_count = 0;
    for (int i = 0; i < 50000; i++) {
        int count = e1000_poll_rx();
        if (count > 0) {
            rx_count += count;
        }
        // Small delay
        for (volatile int j = 0; j < 10000; j++);
    }
    
    if (rx_count > 0) {
        serial_write("[NET_SELFTEST] PASS N3: Received ");
        serial_write_dec(rx_count);
        serial_write(" packets.\n");
    } else {
        serial_write("[NET_SELFTEST] FAIL N3: No packets received.\n");
    }
}