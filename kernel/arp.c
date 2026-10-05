#include "arp.h"
#include "e1000.h"
#include "string.h"
#include "serial.h"
#include "timer.h"

arp_entry_t arp_table[ARP_MAX_ENTRIES];

void arp_init(void) {
    memset(arp_table, 0, sizeof(arp_table));
    serial_write("[ARP] ARP table initialized\n");
}

int arp_lookup(uint32_t ip, uint8_t *mac_out) {
    for (int i = 0; i < ARP_MAX_ENTRIES; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip) {
            memcpy(mac_out, arp_table[i].mac, 6);
            arp_table[i].last_used = timer_ticks();
            return 0;
        }
    }
    return -1;
}

void arp_update(uint32_t ip, const uint8_t *mac) {
    // First try to find existing entry
    for (int i = 0; i < ARP_MAX_ENTRIES; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip) {
            memcpy(arp_table[i].mac, mac, 6);
            arp_table[i].last_used = timer_ticks();
            serial_write("[ARP] Updated entry: IP=");
            serial_write_hex(ip);
            serial_write(" MAC=");
            for (int j = 0; j < 6; j++) {
                serial_write_hex(mac[j]);
                if (j < 5) serial_write(":");
            }
            serial_write("\n");
            return;
        }
    }
    
    // Find empty slot
    for (int i = 0; i < ARP_MAX_ENTRIES; i++) {
        if (!arp_table[i].valid) {
            arp_table[i].valid = 1;
            arp_table[i].ip = ip;
            memcpy(arp_table[i].mac, mac, 6);
            arp_table[i].last_used = timer_ticks();
            serial_write("[ARP] New entry: IP=");
            serial_write_hex(ip);
            serial_write(" MAC=");
            for (int j = 0; j < 6; j++) {
                serial_write_hex(mac[j]);
                if (j < 5) serial_write(":");
            }
            serial_write("\n");
            return;
        }
    }
    
    // Table full - replace oldest
    int oldest = 0;
    for (int i = 1; i < ARP_MAX_ENTRIES; i++) {
        if (arp_table[i].last_used < arp_table[oldest].last_used) {
            oldest = i;
        }
    }
    arp_table[oldest].valid = 1;
    arp_table[oldest].ip = ip;
    memcpy(arp_table[oldest].mac, mac, 6);
    arp_table[oldest].last_used = timer_ticks();
    serial_write("[ARP] Replaced oldest entry: IP=");
    serial_write_hex(ip);
    serial_write("\n");
}

void arp_handle_request(const uint8_t *frame, uint16_t len) {
    if (len < 28) return; // Minimum ARP packet
    
    // ARP starts at offset 14 (Ethernet header)
    const uint8_t *arp = frame + 14;
    uint16_t hw_type = *(uint16_t *)(arp + 0);
    uint16_t proto_type = *(uint16_t *)(arp + 2);
    uint8_t hw_size = arp[4];
    uint8_t proto_size = arp[5];
    uint16_t opcode = *(uint16_t *)(arp + 6);
    
    // Only handle Ethernet/IPv4 ARP requests
    if (hw_type != 0x0100 || proto_type != 0x0008 || hw_size != 6 || proto_size != 4) {
        return;
    }
    
    if (opcode != 0x0100) return; // Only handle requests
    
    uint8_t sender_mac[6];
    uint32_t sender_ip;
    uint32_t target_ip;
    
    memcpy(sender_mac, arp + 8, 6);
    sender_ip = *(uint32_t *)(arp + 14);
    target_ip = *(uint32_t *)(arp + 24);
    
    // Update ARP table with sender info
    arp_update(sender_ip, sender_mac);
    
    // Check if target IP is our IP (10.0.2.15 = 0x0F02000A)
    if (target_ip != 0x0F02000A) {
        return;
    }
    
    // Send ARP reply
    serial_write("[ARP] Sending ARP reply for ");
    serial_write_hex(target_ip);
    serial_write("\n");
    
    // Build ARP reply
    uint8_t reply[42]; // 14 (eth) + 28 (arp)
    uint8_t *eth = reply;
    uint8_t *arp_reply = reply + 14;
    
    // Ethernet header
    memcpy(eth + 0, sender_mac, 6);  // dst = sender
    memcpy(eth + 6, e1000_dev.mac, 6); // src = us
    *(uint16_t *)(eth + 12) = 0x0608; // 0x0806 little endian
    
    // ARP reply
    *(uint16_t *)(arp_reply + 0) = 0x0100; // hw_type = 1 (Ethernet)
    *(uint16_t *)(arp_reply + 2) = 0x0008; // proto_type = 0x0800 (IPv4)
    arp_reply[4] = 6; // hw_size
    arp_reply[5] = 4; // proto_size
    *(uint16_t *)(arp_reply + 6) = 0x0200; // opcode = 2 (reply)
    memcpy(arp_reply + 8, e1000_dev.mac, 6); // sender_mac = our MAC
    *(uint32_t *)(arp_reply + 14) = 0x0F02000A; // sender_ip = 10.0.2.15
    memcpy(arp_reply + 18, sender_mac, 6); // target_mac = sender MAC
    *(uint32_t *)(arp_reply + 24) = sender_ip; // target_ip = sender IP
    
    e1000_transmit(reply, sizeof(reply));
}

void arp_send_request(uint32_t target_ip) {
    serial_write("[ARP] Sending ARP request for ");
    serial_write_hex(target_ip);
    serial_write("\n");
    
    // Build ARP request (broadcast)
    uint8_t req[42];
    uint8_t *eth = req;
    uint8_t *arp_req = req + 14;
    
    // Ethernet header - broadcast
    memset(eth + 0, 0xFF, 6); // dst = broadcast
    memcpy(eth + 6, e1000_dev.mac, 6); // src = us
    *(uint16_t *)(eth + 12) = 0x0608; // 0x0806
    
    // ARP request
    *(uint16_t *)(arp_req + 0) = 0x0100;
    *(uint16_t *)(arp_req + 2) = 0x0008;
    arp_req[4] = 6;
    arp_req[5] = 4;
    *(uint16_t *)(arp_req + 6) = 0x0100; // opcode = 1 (request)
    memcpy(arp_req + 8, e1000_dev.mac, 6); // sender_mac = our MAC
    *(uint32_t *)(arp_req + 14) = 0x0F02000A; // sender_ip = 10.0.2.15
    memset(arp_req + 18, 0, 6); // target_mac = 0
    *(uint32_t *)(arp_req + 24) = target_ip; // target_ip
    
    e1000_transmit(req, sizeof(req));
}

void arp_timeout_check(void) {
    uint64_t now = timer_ticks();
    for (int i = 0; i < ARP_MAX_ENTRIES; i++) {
        if (arp_table[i].valid && (now - arp_table[i].last_used) > ARP_TIMEOUT_TICKS) {
            arp_table[i].valid = 0;
            serial_write("[ARP] Entry expired: IP=");
            serial_write_hex(arp_table[i].ip);
            serial_write("\n");
        }
    }
}