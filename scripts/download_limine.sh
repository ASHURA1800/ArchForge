#!/bin/bash
# Script to download and extract Limine bootloader binaries
# Updated for Limine v12.6.1 (latest stable as of 2026-10-04)

set -e

LIMINE_VERSION="v12.6.1"
LIMINE_BINARY_URL="https://github.com/limine-bootloader/limine/releases/download/${LIMINE_VERSION}/limine-binary.tar.gz"
LIMINE_SOURCE_URL="https://github.com/limine-bootloader/limine/releases/download/${LIMINE_VERSION}/limine-${LIMINE_VERSION#v}.tar.gz"
BUILD_DIR="build"
LIMINE_DIR="${BUILD_DIR}/limine"

echo "=== Limine Bootloader Downloader ==="
echo "Version: ${LIMINE_VERSION}"
echo ""

# Clean previous download if present
if [ -d "${LIMINE_DIR}" ]; then
    echo "Removing previous Limine installation at ${LIMINE_DIR}..."
    rm -rf "${LIMINE_DIR}"
fi

mkdir -p "${LIMINE_DIR}/bin"

# ---- Download binary tarball (contains limine-bios.sys, limine-bios-cd.bin, limine.c, etc.) ----
echo "[1/3] Downloading binary tarball..."
curl -L -o "${LIMINE_DIR}/limine-binary.tar.gz" "${LIMINE_BINARY_URL}"

echo "[2/3] Extracting binaries..."
tar -xzf "${LIMINE_DIR}/limine-binary.tar.gz" -C "${LIMINE_DIR}" --strip-components=1

# ---- Download source tarball (needed for host Makefile) ----
echo "[3/3] Downloading source tarball..."
curl -L -o "${LIMINE_DIR}/limine-source.tar.gz" "${LIMINE_SOURCE_URL}"

echo "Extracting source..."
TMP_SRC=$(mktemp -d)
tar -xzf "${LIMINE_DIR}/limine-source.tar.gz" -C "${TMP_SRC}" --strip-components=1

# Build limine-deploy (the host tool) from source
# limine.c is already extracted from binary tarball
echo "Building limine-deploy host tool..."
# Copy the host Makefile
if [ -f "${TMP_SRC}/host/host.mk" ]; then
    cp "${TMP_SRC}/host/host.mk" "${LIMINE_DIR}/Makefile.host"
    (cd "${LIMINE_DIR}" && make -f Makefile.host limine-deploy 2>/dev/null || \
     ${CC:-cc} -O2 -Wall -Wextra -o "${LIMINE_DIR}/bin/limine-deploy" "${LIMINE_DIR}/limine.c")
else
    # Fallback: compile directly
    ${CC:-cc} -O2 -Wall -Wextra -o "${LIMINE_DIR}/bin/limine-deploy" "${LIMINE_DIR}/limine.c"
fi

rm -rf "${TMP_SRC}"

# ---- Verify expected files exist ----
echo ""
echo "Verifying installation..."
REQUIRED_FILES=(
    "${LIMINE_DIR}/limine-bios.sys"
    "${LIMINE_DIR}/limine-bios-cd.bin"
    "${LIMINE_DIR}/limine-uefi-cd.bin"
    "${LIMINE_DIR}/BOOTX64.EFI"
    "${LIMINE_DIR}/bin/limine-deploy"
)

ALL_OK=true
for f in "${REQUIRED_FILES[@]}"; do
    if [ -f "$f" ]; then
        echo "  ✓ $(basename $f)"
    else
        echo "  ✗ MISSING: $f"
        ALL_OK=false
    fi
done

if [ "$ALL_OK" = true ]; then
    echo ""
    echo "=== Limine ${LIMINE_VERSION} installed successfully ==="
    echo "Location: ${LIMINE_DIR}/"
    echo ""
    echo "You can now use 'make' to build ArchForge OS."
else
    echo ""
    echo "WARNING: Some required files are missing. Build may fail."
    exit 1
fi
