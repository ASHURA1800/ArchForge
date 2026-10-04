#!/bin/bash
# Build user-space programs

set -e

USER_DIR="/home/sagar/Projects/ArchForge/user"
BUILD_DIR="/home/sagar/Projects/ArchForge/build/user"

mkdir -p "$BUILD_DIR"

echo "Building user programs..."

# Compile hello.c
clang -target x86_64-elf -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
      -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel -Wall -Wextra \
      -O2 -c "$USER_DIR/hello.c" -o "$BUILD_DIR/hello.o"

# Link with user.ld
ld.lld -nostdlib -T "$USER_DIR/user.ld" -o "$BUILD_DIR/hello.elf" "$BUILD_DIR/hello.o"

echo "User programs built:"
ls -la "$BUILD_DIR/"

# Verify ELF
readelf -h "$BUILD_DIR/hello.elf" | grep -E "(Entry point|Type|Machine|Class)"