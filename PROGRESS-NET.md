# ArchForge OS Networking Sprint Progress

## Iteration 0: Infrastructure
**Milestone:** N0 - Infrastructure setup, test harness, and risk mitigation.
**Change:** 
- Created `scripts/net_host.py` with UDP echo (5556), TCP echo (5557), TCP sink (5558), TCP source (5559), and HTTP server (8080).
- Updated `scripts/test.sh` to support the `net` suite with QEMU e1000 NIC, slirp user networking, and pcap dumping.
- Bumped `MAX_SYSCALLS` to 32 in `include/syscall.h` and added stubs for SYS_SOCKET through SYS_SHUTDOWN.
- Added `wait_queue_t`, `process_block()`, and `process_wake_all()` to `include/process.h` and `kernel/process.c` to support blocking syscalls (e.g., recv, accept).
- Verified Makefile already uses `-MMD -MP` for header dependencies and `-M pc` for QEMU.
**Build:** ✅ OK
**Tests:** N/A (Harness setup only)
**Next Action:** N1 - PCI config space read/write and e1000 device detection.