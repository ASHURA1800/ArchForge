#!/bin/bash
# ArchForge OS Test Harness
# Usage: scripts/test.sh <suite>
# Suites: smoke | unit | persist | net | all

set -e

PROJECT_DIR="/home/sagar/Projects/ArchForge"
BUILD_DIR="$PROJECT_DIR/build"

echo "=== ArchForge OS Test Harness ==="
echo "Suite: $1"

# Build for each test
echo "Building..."
make clean > /dev/null 2>&1 || true
make 2>&1 | tee "$BUILD_DIR/build.log"

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
        echo "Running unit tests (kernel test mode)..."
        make clean > /dev/null 2>&1 || true
        make TEST=1 2>&1 | tee "$BUILD_DIR/build_unit.log"
        
        timeout 60 qemu-system-x86_64 -M pc -m 512M -display none -no-reboot -no-shutdown \
            -serial file:"$BUILD_DIR/serial_unit.log" \
            -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
            -d guest_errors,cpu_reset -D "$BUILD_DIR/qemu_unit.log" \
            -cdrom "$BUILD_DIR/archforge.iso" \
            || true
       
        # Check for unit test results
        if grep -q "\[TEST\] All unit tests passed" "$BUILD_DIR/serial_unit.log" || \
           grep -q "\[UNIT\] All tests passed" "$BUILD_DIR/serial_unit.log"; then
            echo "✅ UNIT TESTS PASSED"
            exit 0
        else
            echo "❌ UNIT TESTS FAILED"
            echo "Last 100 lines of serial log:"
            tail -100 "$BUILD_DIR/serial_unit.log"
            exit 1
        fi
        ;;
    
    persist)
        echo "Running persistence test (2 boots)..."
        # Build non-test image
        make clean > /dev/null 2>&1 || true
        make 2>&1 | tee "$BUILD_DIR/build.log"
        
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
        echo "=== Analyzing results ==="
        
        BOOT1_OK=0
        BOOT2_OK=0
        PERSIST_PASS=0
        
        if grep -q "Welcome to ArchForge OS" "$BUILD_DIR/serial1.log"; then
            BOOT1_OK=1
            echo "✅ Boot 1: Kernel started"
        else
            echo "❌ Boot 1: Kernel did not start"
        fi
        
        if grep -q "\[PERSIST\] Created persist.dat" "$BUILD_DIR/serial1.log"; then
            echo "✅ Boot 1: persist.dat created"
        else
            echo "⚠️  Boot 1: persist.dat creation not found"
        fi
        
        if grep -q "Welcome to ArchForge OS" "$BUILD_DIR/serial2.log"; then
            BOOT2_OK=1
            echo "✅ Boot 2: Kernel started"
        else
            echo "❌ Boot 2: Kernel did not start"
        fi
        
        if grep -q "\[PERSIST\] PASS" "$BUILD_DIR/serial2.log"; then
            PERSIST_PASS=1
            echo "✅ Boot 2: Persistence verified!"
        elif grep -q "\[PERSIST\] FAIL" "$BUILD_DIR/serial2.log"; then
            echo "❌ Boot 2: Persistence FAILED (data mismatch)"
        else
            echo "⚠️  Boot 2: No persistence result found"
        fi
        
        if [ "$BOOT1_OK" -eq 1 ] && [ "$BOOT2_OK" -eq 1 ] && [ "$PERSIST_PASS" -eq 1 ]; then
            echo ""
            echo "✅ PERSISTENCE TEST PASSED"
            exit 0
        else
            echo ""
            echo "❌ PERSISTENCE TEST FAILED"
            echo "=== Boot 1 serial (last 30 lines) ==="
            tail -30 "$BUILD_DIR/serial1.log"
            echo "=== Boot 2 serial (last 30 lines) ==="
            tail -30 "$BUILD_DIR/serial2.log"
            exit 1
        fi
        ;;

    net)
        echo "Running network test (N3: e1000 polling)..."
        
        # Build with TEST=1
        make clean > /dev/null 2>&1 || true
        make TEST=1 2>&1 | tee "$BUILD_DIR/build_net.log"
        
        # Clean old logs
        rm -f "$BUILD_DIR/serial.log" "$BUILD_DIR/qemu.log" "$BUILD_DIR/net.pcap"
       
        echo "[TEST] Starting QEMU with e1000 NIC (slirp)..."
        timeout 90 qemu-system-x86_64 -M pc -m 512M -display none -no-reboot -no-shutdown \
            -serial file:"$BUILD_DIR/serial.log" \
            -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
            -netdev user,id=n0,hostfwd=udp::5556-:5556,hostfwd=tcp::5555-:5555 \
            -device e1000,netdev=n0 \
            -object filter-dump,id=f0,netdev=n0,file="$BUILD_DIR/net.pcap" \
            -d guest_errors,cpu_reset -D "$BUILD_DIR/qemu.log" \
            -drive format=raw,file="$BUILD_DIR/archforge.iso" \
            || true
           
        echo "[TEST] QEMU exited. Analyzing results..."
       
        # Check for N3 pass in serial log
        if grep -q "\[NET_SELFTEST\] PASS N3" "$BUILD_DIR/serial.log"; then
            echo "✅ NETWORK TEST PASSED (N3: e1000 polling)"
           
            # Verify pcap has ARP request
            if command -v tcpdump >/dev/null 2>&1; then
                ARP_COUNT=$(tcpdump -nn -r "$BUILD_DIR/net.pcap" arp 2>/dev/null | grep -c "who-has" || true)
                if [ "$ARP_COUNT" -gt 0 ]; then
                    echo "✅ PCAP VERIFIED: Found $ARP_COUNT ARP request(s)"
                else
                    echo "⚠️  PCAP WARNING: No ARP requests found in net.pcap"
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
    
    gfx)
        echo "Running graphics test..."
        
        rm -f "$BUILD_DIR/serial.log" "$BUILD_DIR/qemu.log" "$BUILD_DIR/mon.sock"
        rm -rf "$BUILD_DIR/shots"
        mkdir -p "$BUILD_DIR/shots"
        
        echo "[TEST] Starting QEMU with monitor socket..."
        timeout 60 qemu-system-x86_64 -M pc -m 512M -display none -no-reboot -no-shutdown \
            -serial file:"$BUILD_DIR/serial.log" \
            -monitor unix:"$BUILD_DIR/mon.sock",server,nowait \
            -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
            -d guest_errors,cpu_reset -D "$BUILD_DIR/qemu.log" \
            -cdrom "$BUILD_DIR/archforge.iso" &
        QEMU_PID=$!
        
        sleep 2
        
        wait_and_dump() {
            local marker=$1
            local out_ppm=$2
            echo "Waiting for marker: $marker"
            for i in {1..30}; do
                if grep -q "$marker" "$BUILD_DIR/serial.log"; then
                    echo "Marker found. Taking screendump..."
                    python3 "$PROJECT_DIR/scripts/qemu_ctl.py" "$BUILD_DIR/mon.sock" screendump "$out_ppm"
                    return 0
                fi
                sleep 0.5
            done
            echo "Timeout waiting for marker: $marker"
            return 1
        }
        
        wait_and_dump "[GFX] FILL RED" "$BUILD_DIR/shots/red.ppm"
        wait_and_dump "[GFX] FILL GREEN" "$BUILD_DIR/shots/green.ppm"
        wait_and_dump "[GFX] FILL BLUE" "$BUILD_DIR/shots/blue.ppm"
        
        sleep 2
        python3 "$PROJECT_DIR/scripts/qemu_ctl.py" "$BUILD_DIR/mon.sock" quit 2>/dev/null || true
        kill $QEMU_PID 2>/dev/null || true
        wait $QEMU_PID 2>/dev/null || true
        
        echo "[TEST] QEMU exited. Analyzing graphics results..."
        
        if ! grep -q "\[TEST\] PASS gfx" "$BUILD_DIR/serial.log"; then
            echo "❌ GRAPHICS TEST FAILED: Guest did not report success"
            tail -50 "$BUILD_DIR/serial.log"
            exit 1
        fi
        
        if [ ! -f "$BUILD_DIR/shots/red.ppm" ] || [ ! -f "$BUILD_DIR/shots/green.ppm" ] || [ ! -f "$BUILD_DIR/shots/blue.ppm" ]; then
            echo "❌ GRAPHICS TEST FAILED: Missing screendumps"
            exit 1
        fi
        
        python3 "$PROJECT_DIR/scripts/gfx_check.py" verify_colors "$BUILD_DIR/shots"
        
        echo "✅ GRAPHICS TEST PASSED"
        exit 0
        ;;
    
    all)
        echo "Running all tests..."
        "$0" smoke && "$0" unit && "$0" persist && "$0" net && "$0" gfx
        ;;
    
    *)
        echo "Usage: $0 <smoke|unit|persist|net|all>"
        exit 1
        ;;
esac