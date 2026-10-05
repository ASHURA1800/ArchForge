# ArchForge OS Network Sprint Progress

## Sprint: sprint-net (Networking)

### Milestones

| Milestone | Status | Description |
|-----------|--------|-------------|
| N0 | ✅ DONE | Test harness, syscall stubs, process blocking |
| N1 | ✅ DONE | PCI config space, e1000 detection, bus mastering |
| N2 | ✅ DONE | DMA + MMIO plumbing, e1000 MMIO mapping and reset |
| N3 | ✅ DONE | e1000 polling driver - TX ARP request, RX ARP reply |

## Sprint: sprint-tcp (TCP/IP Stack)

### Milestones

| Milestone | Status | Description |
|-----------|--------|-------------|
| N4 | 🔄 IN PROGRESS | ARP table, ICMP echo reply |
| N5 | ⏳ PENDING | UDP stack, DNS client |
| N6 | ⏳ PENDING | TCP state machine, HTTP client |

### N4: ARP Table + ICMP Echo Reply

**Pass Criteria**:
- [ ] ARP table caches MAC/IP mappings (timeout-based eviction)
- [ ] ICMP echo request → echo reply (valid checksum, correct IDs)
- [ ] Verified via ping from host (10.0.2.2 → 10.0.2.15)
- [ ] PCAP shows ICMP request + reply

**Implementation Plan**:
1. ARP table (hash map or linear array, max 16 entries, 60s timeout)
2. ICMP packet structure (type 8 request, type 0 reply)
3. ICMP checksum calculation
4. Echo reply handler in poll_rx path
5. Integration test: ping from host

### N5: UDP Stack + DNS Client

**Pass Criteria**:
- [ ] UDP packet structure with checksum (pseudo-header)
- [ ] UDP "socket" API (bind, sendto, recvfrom - polling)
- [ ] DNS query construction (A record, recursion desired)
- [ ] DNS response parsing (answer section, A records)
- [ ] Verified: DNS query for example.com → A record response

**Implementation Plan**:
1. UDP header + pseudo-header checksum
2. UDP receive buffer + port demux
3. DNS message format (header, question, answer)
4. DNS client: query → parse response
5. Test: resolve example.com via 10.0.2.3 (slirp DNS)

### N6: TCP State Machine + HTTP Client

**Pass Criteria**:
- [ ] TCP state machine (CLOSED→SYN_SENT→ESTABLISHED→FIN_WAIT→CLOSED)
- [ ] SYN/SYN-ACK/ACK handshake
- [ ] Sequence/ACK number management
- [ ] HTTP/1.0 GET request → response parsing
- [ ] Verified: GET http://10.0.2.2:8080/ → 200 OK

**Implementation Plan**:
1. TCP header + flags + checksum (pseudo-header)
2. TCP state machine (per-connection struct)
3. Retransmission timer + RTO calculation
4. HTTP request builder / response parser
5. Test: HTTP GET to host Python server

### Technical Details

**Hardware**: Intel 82540EM (QEMU e1000)
- MMIO: 0xFFFFC00000000000 (outside HHDM + heap)
- Legacy 16-byte descriptors, 32 descriptors (512 bytes = 1 page)
- RX ring: RDT=31, TX ring: 32 descriptors
- MAC: 52:54:00:12:34:56 (our), Gateway: 52:55:0a:00:02:02

**Slirp Network** (QEMU user-mode):
- Guest IP: 10.0.2.15
- Gateway/DNS: 10.0.2.2 (MAC 52:55:0a:00:02:02)
- DNS: 10.0.2.3
- Host loopback: 10.0.2.2 (forwards to 127.0.0.1)

### Verification Commands

```bash
# N4: ARP table + ICMP
scripts/net_verify.sh --only N4
# Host: ping 10.0.2.15

# N5: UDP + DNS
scripts/net_verify.sh --only N5
# Guest: dns_resolve("example.com")

# N6: TCP + HTTP
scripts/net_verify.sh --only N6
# Guest: http_get("http://10.0.2.2:8080/")
```

### File Structure (planned)

```
kernel/
├── net.h              # Common network types
├── arp.h/c            # ARP table, cache
├── icmp.h/c           # ICMP echo
├── udp.h/c            # UDP stack
├── dns.h/c            # DNS client
├── tcp.h/c            # TCP state machine
├── http.h/c           # HTTP client
└── net_selftest.c     # Updated for N4-N6
```