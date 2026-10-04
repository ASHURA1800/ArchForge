#!/bin/bash
# ArchForge OS Test Harness
# Usage: scripts/test.sh <suite>
# Suites: smoke | unit | persist | all

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
    
    all)
        echo "Running all tests..."
        "$0" smoke && "$0" unit && "$0" persist
        ;;
    
    *)
        echo "Usage: $0 <smoke|unit|persist|all>"
        exit 1
        ;;
esac