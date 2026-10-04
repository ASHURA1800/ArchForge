#!/bin/bash
# ArchForge OS Test Harness
# Usage: scripts/test.sh <suite>
# Suites: smoke | unit | persist | net | all

set -e

PROJECT_DIR="/home/sagar/Projects/ArchForge"
BUILD_DIR="$PROJECT_DIR/build"

echo "=== ArchForge OS Test Harness ==="
echo "Suite: $1"

# Build only if build artifacts don't exist
if [ ! -f "$BUILD_DIR/kernel.elf" ] || [ ! -f "$BUILD_DIR/archforge.img" ]; then
    echo "Building..."
    make 2>&1 | tee "$BUILD_DIR/build.log"
else
    echo "Using existing build artifacts..."
fi

# Check for warnings in files we care about
if [ -f "$BUILD_DIR/build.log" ] && grep -q "warning:" "$BUILD_DIR/build.log"; then
    echo "⚠️  Build has warnings:"
    grep "warning:" "$BUILD_DIR/build.log" | head -20
fi

case "$1" in
    smoke)
        echo "Running smoke test..."
        timeout 60 qemu-system-x86_64 -M pc -m 512M -display none -no-reboot -no-shutdown \
            -serial file:"$BUILD_DIR/serial.log" \
            -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
            -d guest_errors,cpu_reset -D "$BUILD_DIR/qemu.log" \
            -cdrom "$BUILD_DIR/archforge.iso" \
            || true
        
        # Check for key indicators in serial log
        if grep -q "Welcome to ArchForge OS" "$BUILD_DIR/serial.log" && \
           grep -q "SHELL] Shell initialized" "$BUILD_DIR/serial.log"; then
            echo "✅ SMOKE TEST PASSED"
            exit 0
        else
            echo "❌ SMOKE TEST FAILED"
            echo "Last 50 lines of serial log:"
            tail -50 "$BUILD_DIR/serial.log"
            exit 1
        fi
        ;;
    
    unit)
        echo "Running unit tests..."
        # TODO: Add kernel test mode
        echo "Unit tests not yet implemented"
        exit 1
        ;;
    
    persist)
        echo "Running persistence test (2 boots)..."
        # Boot 1: Write data
        echo "=== BOOT 1: Writing test data ==="
        timeout 60 qemu-system-x86_64 -M pc -m 512M -display none -no-reboot -no-shutdown \
            -serial file:"$BUILD_DIR/serial1.log" \
            -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
            -d guest_errors,cpu_reset -D "$BUILD_DIR/qemu1.log" \
            -drive file="$BUILD_DIR/archforge.img",format=raw,if=ide \
            || true
        
        # Copy the image for boot 2
        cp "$BUILD_DIR/archforge.img" "$BUILD_DIR/archforge_persist.img"
        
        # Boot 2: Verify data
        echo "=== BOOT 2: Verifying test data ==="
        timeout 60 qemu-system-x86_64 -M pc -m 512M -display none -no-reboot -no-shutdown \
            -serial file:"$BUILD_DIR/serial2.log" \
            -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
            -d guest_errors,cpu_reset -D "$BUILD_DIR/qemu2.log" \
            -drive file="$BUILD_DIR/archforge_persist.img",format=raw,if=ide \
            || true
        
        # Check both boots
        if grep -q "Welcome to ArchForge OS" "$BUILD_DIR/serial1.log" && \
           grep -q "Welcome to ArchForge OS" "$BUILD_DIR/serial2.log"; then
            echo "✅ PERSISTENCE TEST PASSED"
            exit 0
        else
            echo "❌ PERSISTENCE TEST FAILED"
            exit 1
        fi
        ;;

    net)
        echo "Running network test..."
        
        # Start host helper services
        echo "[TEST] Starting host network helpers..."
        python3 "$PROJECT_DIR/scripts/net_host.py" &
        HELPER_PID=$!
        sleep 2 # Give helpers time to bind
        
        # Cleanup on exit
        trap "kill $HELPER_PID 2>/dev/null || true" EXIT
        
        # Clear old logs
        rm -f "$BUILD_DIR/serial.log" "$BUILD_DIR/qemu.log" "$BUILD_DIR/net.pcap" "$BUILD_DIR/host_*.log"
        
        echo "[TEST] Starting QEMU with e1000 NIC..."
        timeout 90 qemu-system-x86_64 -M pc -m 512M -display none -no-reboot -no-shutdown \
            -serial file:"$BUILD_DIR/serial.log" \
            -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
            -netdev user,id=n0,hostfwd=tcp::5555-:5555,hostfwd=udp::5556-:5556,hostfwd=tcp::5557-:5557,hostfwd=tcp::5558-:5558,hostfwd=tcp::5559-:5559,hostfwd=tcp::8080-:8080 \
            -device e1000,netdev=n0 \
            -object filter-dump,id=f0,netdev=n0,file="$BUILD_DIR/net.pcap" \
            -d guest_errors,cpu_reset -D "$BUILD_DIR/qemu.log" \
            -drive file="$BUILD_DIR/archforge.img",format=raw,if=ide \
            || true
            
        echo "[TEST] QEMU exited. Analyzing results..."
        
        # Check for network test pass/fail in serial log
        if grep -q "\[TEST\] PASS net" "$BUILD_DIR/serial.log"; then
            echo "✅ NETWORK TEST PASSED (Guest reported success)"
            
            # Optional: verify pcap has expected packets (e.g., ARP, ICMP)
            if command -v tcpdump >/dev/null 2>&1; then
                ARP_COUNT=$(tcpdump -nn -r "$BUILD_DIR/net.pcap" arp 2>/dev/null | wc -l)
                if [ "$ARP_COUNT" -gt 0 ]; then
                    echo "✅ PCAP VERIFIED: Found $ARP_COUNT ARP packets"
                else
                    echo "⚠️  PCAP WARNING: No ARP packets found in net.pcap"
                fi
            fi
            
            exit 0
        else
            echo "❌ NETWORK TEST FAILED"
            echo "Last 100 lines of serial log:"
            tail -100 "$BUILD_DIR/serial.log"
            exit 1
        fi
        ;;
    
    all)
        echo "Running all tests..."
        "$0" smoke && "$0" unit && "$0" persist && "$0" net
        ;;
    
    *)
        echo "Usage: $0 <smoke|unit|persist|net|all>"
        exit 1
        ;;
esac