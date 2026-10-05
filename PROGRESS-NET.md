# ArchForge OS Network Sprint Progress

## Sprint: sprint-net (Networking)

### Milestones

| Milestone | Status | Description |
|-----------|--------|-------------|
| N0 | ✅ DONE | Test harness, syscall stubs, process blocking |
| N1 | ✅ DONE | PCI config space, e1000 detection, bus mastering |
| N2 | ✅ DONE | DMA + MMIO plumbing, e1000 MMIO mapping and reset |
| N3 | ✅ DONE | e1000 polling driver - TX ARP request, RX ARP reply |

### N3: e1000 Polling Driver - COMPLETE

**Pass Criteria (D6)**: 
- ✅ TX ARP request transmitted (DD=1 confirmed)
- ✅ RX ARP reply received (opcode 2, sender 10.0.2.2)
- ✅ PCAP verification at host level (net.pcap shows ARP request + reply)

**Defects Fixed (D1-D5)**:
- ✅ D1: MMIO VA collision - moved to 0xFFFFC00000000000
- ✅ D2: RX polling logic - software rx_cur cursor, volatile reads, compiler barriers, RDT writes
- ✅ D3: TX wait loop - volatile descriptor reads, DD bit wait with timeout
- ✅ D4: Init order/hygiene - IMC=0xFFFFFFFF, ICR clear, MTA cleared, TIPG/TCTL set, SLU polling, 1-page rings
- ✅ D5: Diagnostics on failure - STATUS, RCTL, RDH/RDT, RX descriptor status, frame dumps, RXC counter

**Additional Fixes (D7-D11)**:
- ✅ D7: scripts/net_verify.sh - removed CFLAGS_EXTRA, clean-net target, --only/--through flags
- ✅ D8: Separate TEST mode boot - boot/limine-net.conf, LIMINE_CFG variable
- ✅ D9: scripts/test.sh - explicit builds, unit test suite
- ✅ D10: PROGRESS-NET.md + warn_baseline.txt created
- ✅ D11: Makefile - e1000.c, net_selftest.c tracked in link list

### Technical Details

**Hardware**: Intel 82540EM (QEMU e1000)
- MMIO: 0xFFFFC00000000000 (outside HHDM + heap)
- Legacy 16-byte descriptors, 32 descriptors (512 bytes = 1 page)
- RX ring: RDT=31, TX ring: 32 descriptors
- MAC: 52:54:00:12:34:56 (our), Gateway: 52:55:0a:00:02:02

**RCTL Configuration**: 0x400803e
- EN (1), BAM (15), SZ_2048 (0), SECRC (26), UPE (3)
- MPE (4), SBP (2), VPE (1), LPE (5)

**TX Path**: Working - DD bit confirmed
**RX Path**: Working - ARP reply received, rx_cur advanced

### Verification

```bash
# Run N3 verification
scripts/net_verify.sh --only N3

# Run full network test
scripts/test.sh net

# Check pcap
tcpdump -nn -r build/net.pcap
```

### Next Sprint (sprint-tcp)

- N4: ARP table, ICMP echo reply
- N5: UDP stack, DNS client
- N6: TCP state machine, HTTP client