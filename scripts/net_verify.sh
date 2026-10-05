#!/bin/bash
# ArchForge OS Network Verification Script

set -e

BUILD_DIR="build"
SERIAL_LOG="$BUILD_DIR/serial.log"
QEMU_LOG="$BUILD_DIR/qemu.log"
PCAP_FILE="$BUILD_DIR/net.pcap"

# Ensure build directory exists
mkdir -p "$BUILD_DIR"

# Parse arguments
ONLY_TEST=""
THROUGH_TEST=""
if [ "$1" == "--only" ]; then
    ONLY_TEST="$2"
fi
if [ "$1" == "--through" ]; then
    THROUGH_TEST="$2"
fi

echo "=== ArchForge OS Network Verification ==="
echo "Suite: ${ONLY_TEST:-all} ${THROUGH_TEST:+through $THROUGH_TEST}"

# Clean previous logs
rm -f "$SERIAL_LOG" "$QEMU_LOG" "$PCAP_FILE"

# Build the kernel with TEST=1 (via Makefile variable)
echo "[1/4] Building kernel with TEST=1..."
make clean > /dev/null 2>&1 || true
mkdir -p "$BUILD_DIR" && make TEST=1 > "$BUILD_DIR/build.log" 2>&1
if [ $? -ne 0 ]; then
    echo "❌ BUILD FAILED. Check $BUILD_DIR/build.log"
    tail -20 "$BUILD_DIR/build.log"
    exit 1
fi
echo "✅ Build successful."

# Start host helper if needed (for later milestones)
# python3 scripts/net_host.py &
# HOST_PID=$!
# trap "kill $HOST_PID 2>/dev/null" EXIT

echo "[2/4] Starting QEMU with e1000 NIC and slirp..."
timeout 30 qemu-system-x86_64 \
    -M pc -m 512M -display none -no-reboot -no-shutdown \
    -serial file:"$SERIAL_LOG" \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -netdev user,id=n0,hostfwd=udp::5556-:5556,hostfwd=tcp::5555-:5555 \
    -device e1000,netdev=n0 \
    -object filter-dump,id=f0,netdev=n0,file="$PCAP_FILE" \
    -d guest_errors,cpu_reset -D "$QEMU_LOG" \
    -drive format=raw,file="$BUILD_DIR/archforge.iso" \
    || true

echo "[3/4] Analyzing results..."

# Check for QEMU crashes
if grep -q "cpu_reset" "$QEMU_LOG" || grep -q "guest_errors" "$QEMU_LOG"; then
    echo "❌ QEMU crashed or reset. Check $QEMU_LOG"
    exit 1
fi

# Check serial log for N3 pass
if [ -n "$THROUGH_TEST" ]; then
    # Run through all tests up to THROUGH_TEST
    # For now just handle N3
    if [ "$THROUGH_TEST" == "N3" ] || [ -z "$THROUGH_TEST" ]; then
        if grep -q "\[NET_SELFTEST\] PASS N3" "$SERIAL_LOG"; then
            echo "✅ N3: e1000 polling test PASSED"
        else
            echo "❌ N3: e1000 polling test FAILED"
            echo "Serial log tail:"
            tail -20 "$SERIAL_LOG"
            exit 1
        fi
        
        # Check pcap for ARP request
        if command -v tcpdump >/dev/null 2>&1; then
            ARP_COUNT=$(tcpdump -nn -r "$PCAP_FILE" arp 2>/dev/null | grep -c "who-has" || true)
            if [ "$ARP_COUNT" -gt 0 ]; then
                echo "✅ PCAP: Found $ARP_COUNT ARP request(s) in net.pcap"
            else
                echo "⚠️  PCAP: No ARP requests found in net.pcap (might be QEMU slirp behavior)"
            fi
        else
            echo "⚠️  tcpdump not found, skipping pcap verification"
        fi
    fi
elif [ "$ONLY_TEST" == "N3" ] || [ -z "$ONLY_TEST" ]; then
    if grep -q "\[NET_SELFTEST\] PASS N3" "$SERIAL_LOG"; then
        echo "✅ N3: e1000 polling test PASSED"
    else
        echo "❌ N3: e1000 polling test FAILED"
        echo "Serial log tail:"
        tail -20 "$SERIAL_LOG"
        exit 1
    fi
    
    # Check pcap for ARP request
    if command -v tcpdump >/dev/null 2>&1; then
        ARP_COUNT=$(tcpdump -nn -r "$PCAP_FILE" arp 2>/dev/null | grep -c "who-has" || true)
        if [ "$ARP_COUNT" -gt 0 ]; then
            echo "✅ PCAP: Found $ARP_COUNT ARP request(s) in net.pcap"
        else
            echo "⚠️  PCAP: No ARP requests found in net.pcap (might be QEMU slirp behavior)"
        fi
    else
        echo "⚠️  tcpdump not found, skipping pcap verification"
    fi
fi

echo "[4/4] Network verification complete."
exit 0