#ifndef _ARP_H
#define _ARP_H

#include <stdint.h>
#include <stddef.h>

#define ARP_MAX_ENTRIES 16
#define ARP_TIMEOUT_TICKS 60000  // ~60 seconds at 1000Hz timer

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    uint64_t last_used;  // timer ticks
    int valid;
} arp_entry_t;

extern arp_entry_t arp_table[ARP_MAX_ENTRIES];

void arp_init(void);
int arp_lookup(uint32_t ip, uint8_t *mac_out);
void arp_update(uint32_t ip, const uint8_t *mac);
void arp_handle_request(const uint8_t *frame, uint16_t len);
void arp_send_request(uint32_t target_ip);
void arp_timeout_check(void);

#endif