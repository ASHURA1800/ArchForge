# ArchForge OS Makefile

# Toolchain
CC = clang
LD = ld.lld
# Note: .S files are assembled via clang (with C preprocessor), not nasm

# Compiler flags
CFLAGS = -target x86_64-elf -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
         -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel -Wall -Wextra -I./include -O2 \
         -MMD -MP

# Linker flags
LDFLAGS = -nostdlib -T kernel/linker.ld -z max-page-size=0x1000

# Directories
BUILD_DIR = build
KERNEL_DIR = kernel
INCLUDE_DIR = include
BOOT_DIR = boot
ISO_DIR = $(BUILD_DIR)/iso

# Output files
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
ISO_IMAGE = $(BUILD_DIR)/archforge.iso
HDD_IMAGE = $(BUILD_DIR)/archforge.img

# Limine deployment
LIMINE_DIR = $(BUILD_DIR)/limine
LIMINE_CFG = $(BOOT_DIR)/limine.conf
LIMINE_URL = https://github.com/limine-bootloader/limine/releases/download/v12.6.1/limine-binary.tar.gz

# QEMU settings
QEMU = qemu-system-x86_64
QEMU_FLAGS = -M pc -m 512M -serial file:$(BUILD_DIR)/serial.log -display none -no-reboot -no-shutdown \
             -d guest_errors,cpu_reset -D $(BUILD_DIR)/qemu.log \
             -device isa-debug-exit,iobase=0xf4,iosize=0x04
OVMF_PATH = /usr/share/OVMF/OVMF_CODE.fd

.PHONY: all clean run run-uefi run-hdd iso hdd debug verify-iso help

all: $(ISO_IMAGE) $(HDD_IMAGE)

# ---- Compilation rules ----

# Build user programs first
$(BUILD_DIR)/user/hello.elf $(BUILD_DIR)/user/cat.elf $(BUILD_DIR)/user/init.elf $(BUILD_DIR)/user/ls.elf $(BUILD_DIR)/user/cp.elf:
	@mkdir -p $(BUILD_DIR)/user
	$(MAKE) -C user
	cp user/*.elf $(BUILD_DIR)/user/

# Compile C files
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Include generated dependency files
-include $(wildcard $(BUILD_DIR)/*.d)

# Compile assembly files
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Embed user programs as binary objects
$(BUILD_DIR)/user_hello.o: $(BUILD_DIR)/user/hello.elf
	@mkdir -p $(dir $@)
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@
	objcopy --rename-section .data=.data.user_hello $@ $@

$(BUILD_DIR)/user_cat.o: $(BUILD_DIR)/user/cat.elf
	@mkdir -p $(dir $@)
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@
	objcopy --rename-section .data=.data.user_cat $@ $@

$(BUILD_DIR)/user_init.o: $(BUILD_DIR)/user/init.elf
	@mkdir -p $(dir $@)
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

$(BUILD_DIR)/user_ls.o: $(BUILD_DIR)/user/ls.elf
	@mkdir -p $(dir $@)
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

$(BUILD_DIR)/user_cp.o: $(BUILD_DIR)/user/cp.elf
	@mkdir -p $(dir $@)
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

# ---- Link kernel ----

$(KERNEL_ELF): $(BUILD_DIR)/start.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/serial.o \
               $(BUILD_DIR)/gdt.o $(BUILD_DIR)/gdt_asm.o $(BUILD_DIR)/idt.o \
               $(BUILD_DIR)/isrs.o $(BUILD_DIR)/interrupt_handlers.o $(BUILD_DIR)/pmm.o \
               $(BUILD_DIR)/vmm.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/console.o $(BUILD_DIR)/timer.o \
               $(BUILD_DIR)/ramfs.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/string.o $(BUILD_DIR)/stdlib.o \
               $(BUILD_DIR)/shell.o $(BUILD_DIR)/process.o $(BUILD_DIR)/switch.o \
               $(BUILD_DIR)/syscall.o $(BUILD_DIR)/syscall_entry.o $(BUILD_DIR)/msr.o \
               $(BUILD_DIR)/elf.o $(BUILD_DIR)/fat32.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/ata.o $(BUILD_DIR)/pipe.o $(BUILD_DIR)/pci.o \
               $(BUILD_DIR)/user_hello.o $(BUILD_DIR)/user_cat.o $(BUILD_DIR)/user_init.o $(BUILD_DIR)/user_ls.o $(BUILD_DIR)/user_cp.o
	@mkdir -p $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $^

# ---- ISO image ----

# Download limine if missing
$(LIMINE_DIR)/limine-bios.sys:
	@mkdir -p $(LIMINE_DIR)
	@echo "Downloading limine..."
	curl -L -o $(LIMINE_DIR)/limine-binary.tar.gz $(LIMINE_URL)
	tar -xzf $(LIMINE_DIR)/limine-binary.tar.gz -C $(LIMINE_DIR) --strip-components=1
	mkdir -p $(LIMINE_DIR)/bin
	cc -O2 -Wall -Wextra -o $(LIMINE_DIR)/bin/limine-deploy $(LIMINE_DIR)/limine.c

$(ISO_DIR): $(KERNEL_ELF) $(LIMINE_DIR)/limine-bios.sys
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/limine
	@mkdir -p $(ISO_DIR)/limine
	@mkdir -p $(ISO_DIR)/EFI/BOOT
	# Kernel + config at root AND in /boot/
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/kernel.elf
	cp $(LIMINE_CFG) $(ISO_DIR)/limine.conf
	cp $(LIMINE_CFG) $(ISO_DIR)/boot/limine.conf
	# Stage 2 loader under ALL known Limine BIOS search paths
	cp $(LIMINE_DIR)/limine-bios.sys $(ISO_DIR)/boot/limine/limine-bios.sys
	cp $(LIMINE_DIR)/limine-bios.sys $(ISO_DIR)/limine/limine-bios.sys
	cp $(LIMINE_DIR)/limine-bios.sys $(ISO_DIR)/limine-bios.sys
	cp $(LIMINE_DIR)/limine-bios.sys $(ISO_DIR)/limine.sys
	cp $(LIMINE_DIR)/limine-bios.sys $(ISO_DIR)/boot/limine.sys
	# Boot images + UEFI - also copy limine-cd.bin to root for El Torito
	cp $(LIMINE_DIR)/limine-bios-cd.bin  $(ISO_DIR)/boot/limine-bios-cd.bin
	cp $(LIMINE_DIR)/limine-bios-cd.bin  $(ISO_DIR)/boot/limine-cd.bin
	cp $(LIMINE_DIR)/limine-bios-cd.bin  $(ISO_DIR)/limine-cd.bin
	cp $(LIMINE_DIR)/limine-uefi-cd.bin  $(ISO_DIR)/boot/limine-uefi-cd.bin
	cp $(LIMINE_DIR)/BOOTX64.EFI         $(ISO_DIR)/EFI/BOOT/BOOTX64.EFI

$(ISO_IMAGE): $(ISO_DIR)
	@echo "Building bootable ISO..."
	xorriso -as mkisofs -R -r -J -hfsplus -apm-block-size 2048 -b boot/limine-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $(ISO_IMAGE)
	@echo "Deploying Limine bootloader to ISO image..."
	@if [ -f "$(LIMINE_DIR)/bin/limine-deploy" ]; then \
		$(LIMINE_DIR)/bin/limine-deploy bios-install $(ISO_IMAGE); \
	elif command -v limine-deploy >/dev/null 2>&1; then \
		limine-deploy bios-install $(ISO_IMAGE); \
	fi
	@echo "ISO built successfully: $(ISO_IMAGE)"

# ---- HDD image (no-sudo version using mtools) ----

$(HDD_IMAGE): $(KERNEL_ELF)
	@echo "Building HDD image (no-sudo)..."
	@rm -f $(HDD_IMAGE)
	# Create a 64MB raw disk image
	dd if=/dev/zero of=$(HDD_IMAGE) bs=1M count=64 status=none
	# Format entire disk as FAT32 (superfloppy - no partition table)
	mkfs.fat -F 32 $(HDD_IMAGE)
	# Create mtools config for this image (no partition, no offset)
	@echo "drive c: file=\"$(HDD_IMAGE)\"" > $(BUILD_DIR)/mtools.conf
	# Copy kernel and limine files using mtools
	MTOOLSRC=$(BUILD_DIR)/mtools.conf mmd c:/boot c:/boot/limine
	MTOOLSRC=$(BUILD_DIR)/mtools.conf mcopy $(KERNEL_ELF) c:/boot/kernel.elf
	MTOOLSRC=$(BUILD_DIR)/mtools.conf mcopy $(LIMINE_CFG) c:/limine.conf
	MTOOLSRC=$(BUILD_DIR)/mtools.conf mcopy $(LIMINE_CFG) c:/boot/limine.conf
	MTOOLSRC=$(BUILD_DIR)/mtools.conf mcopy $(LIMINE_DIR)/limine-bios.sys c:/boot/limine/limine-bios.sys
	MTOOLSRC=$(BUILD_DIR)/mtools.conf mcopy $(LIMINE_DIR)/limine-bios.sys c:/limine-bios.sys
	MTOOLSRC=$(BUILD_DIR)/mtools.conf mcopy $(LIMINE_DIR)/limine-bios.sys c:/limine.sys
	# Deploy Limine to MBR with --force
	@if [ -f "$(LIMINE_DIR)/bin/limine-deploy" ]; then \
		$(LIMINE_DIR)/bin/limine-deploy bios-install --force $(HDD_IMAGE); \
	elif command -v limine-deploy >/dev/null 2>&1; then \
		limine-deploy bios-install --force $(HDD_IMAGE); \
	fi
	@echo "HDD image built successfully: $(HDD_IMAGE)"

# ---- Test FAT32 image (MBR partitioned, for A1 testing) ----
FAT32_TEST_IMAGE = $(BUILD_DIR)/fat32_test.img

$(FAT32_TEST_IMAGE):
	@echo "Building MBR-partitioned FAT32 test image..."
	@mkdir -p $(BUILD_DIR)
	@rm -f $(FAT32_TEST_IMAGE)
	# Create a 16MB raw disk image
	dd if=/dev/zero of=$(FAT32_TEST_IMAGE) bs=1M count=16 status=none
	# Partition with single FAT32 partition (2048 sector offset = 1MiB)
	parted -s $(FAT32_TEST_IMAGE) mklabel msdos
	parted -s $(FAT32_TEST_IMAGE) mkpart primary fat32 1MiB 100%
	parted -s $(FAT32_TEST_IMAGE) set 1 boot on
	# Format FAT32 on partition using mkfs.fat with offset
	mkfs.fat -F 32 --offset 2048 $(FAT32_TEST_IMAGE)
	# Create mtools config for this image
	@echo "drive d: file=\"$(FAT32_TEST_IMAGE)\" partition=1" > $(BUILD_DIR)/mtools_test.conf
	# Copy test files using mtools
	MTOOLSRC=$(BUILD_DIR)/mtools_test.conf mcopy README.md d:/
	MTOOLSRC=$(BUILD_DIR)/mtools_test.conf mcopy Makefile d:/
	@echo "FAT32 test image built: $(FAT32_TEST_IMAGE)"

# ---- Run targets ----

# Run ISO in QEMU (BIOS mode)
run: $(ISO_IMAGE)
	@echo "Starting QEMU (BIOS mode, ISO)..."
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO_IMAGE) -no-reboot -no-shutdown

# Run ISO in QEMU (UEFI mode)
run-uefi: $(ISO_IMAGE)
	@echo "Starting QEMU (UEFI mode, ISO)..."
	@if [ -f "$(OVMF_PATH)" ]; then \
		$(QEMU) $(QEMU_FLAGS) -bios $(OVMF_PATH) -cdrom $(ISO_IMAGE) -no-reboot -no-shutdown; \
	else \
		echo "ERROR: OVMF not found at $(OVMF_PATH)"; \
		echo "Install with: sudo apt install ovmf  OR  sudo pacman -S edk2-ovmf"; \
		exit 1; \
	fi

# Run HDD image in QEMU (BIOS mode, most reliable)
run-hdd: $(HDD_IMAGE)
	@echo "Starting QEMU (BIOS mode, HDD image)..."
	@if [ -f "$(HDD_IMAGE)" ]; then \
		$(QEMU) $(QEMU_FLAGS) -drive file=$(HDD_IMAGE),format=raw,if=ide -no-reboot -no-shutdown; \
	else \
		echo "ERROR: HDD image not found. Run 'make hdd' first."; \
		exit 1; \
	fi

# Run HDD image with FAT32 test (uses the HDD image)
run-test: $(HDD_IMAGE)
	@echo "Starting QEMU for FAT32 test..."
	@if [ -f "$(HDD_IMAGE)" ]; then \
		$(QEMU) $(QEMU_FLAGS) -drive file=$(HDD_IMAGE),format=raw,if=ide -no-reboot -no-shutdown; \
	else \
		echo "ERROR: HDD image not found. Run 'make hdd' first."; \
		exit 1; \
	fi

# Run FAT32 test image (MBR partitioned, for A1 milestone testing)
run-fat32-test: $(FAT32_TEST_IMAGE)
	@echo "Starting QEMU for FAT32 test image..."
	@if [ -f "$(FAT32_TEST_IMAGE)" ]; then \
		$(QEMU) $(QEMU_FLAGS) -drive file=$(FAT32_TEST_IMAGE),format=raw,if=ide -no-reboot -no-shutdown; \
	else \
		echo "ERROR: FAT32 test image not found. Run 'make $(FAT32_TEST_IMAGE)' first."; \
		exit 1; \
	fi

# Debug in QEMU with GDB
debug: $(ISO_IMAGE)
	@echo "Starting QEMU in debug mode (GDB on port 1234)..."
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO_IMAGE) -s -S

# ---- Verification ----

verify-iso: $(ISO_IMAGE)
	@echo "=== ISO Structure ==="
	@xorriso -indev $(ISO_IMAGE) -ls / 2>&1 | head -30
	@echo ""
	@echo "=== /boot/ contents ==="
	@xorriso -indev $(ISO_IMAGE) -ls /boot 2>&1
	@echo ""
	@echo "=== El Torito Boot Catalog ==="
	@xorriso -indev $(ISO_IMAGE) -report_el_torito plain 2>&1 | head -20
	@echo ""
	@echo "=== Kernel Entry Point ==="
	@readelf -h $(KERNEL_ELF) | grep "Entry point"
	@echo ""
	@echo "=== Limine Requests Section ==="
	@readelf -S $(KERNEL_ELF) | grep limine

# ---- Clean ----

clean:
	rm -rf $(BUILD_DIR)

# ---- Help ----

help:
	@echo "ArchForge OS Build System"
	@echo ""
	@echo "Build targets:"
	@echo "  make          - Build kernel, ISO, and HDD image"
	@echo "  make iso      - Build only the ISO image"
	@echo "  make hdd      - Build only the HDD image"
	@echo ""
	@echo "Run targets:"
	@echo "  make run      - Boot ISO in QEMU (BIOS mode)"
	@echo "  make run-uefi - Boot ISO in QEMU (UEFI mode, needs OVMF)"
	@echo "  make run-hdd  - Boot HDD image in QEMU (BIOS, most reliable)"
	@echo "  make debug    - Boot with GDB server on port 1234"
	@echo ""
	@echo "Other targets:"
	@echo "  make verify-iso - Inspect ISO structure and kernel layout"
	@echo "  make clean      - Remove all build artifacts"
	@echo "  make help       - Show this help message"

# Aliases
iso: $(ISO_IMAGE)
hdd: $(HDD_IMAGE)