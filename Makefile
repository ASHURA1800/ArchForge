# ArchForge OS Makefile

# Toolchain
CC = clang
LD = ld.lld
# Note: .S files are assembled via clang (with C preprocessor), not nasm

# Compiler flags
CFLAGS = -target x86_64-elf -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
         -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel -Wall -Wextra -I./include -O2

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

# QEMU settings
QEMU = qemu-system-x86_64
QEMU_FLAGS = -M q35 -m 2G -serial stdio
OVMF_PATH = /usr/share/OVMF/OVMF_CODE.fd

.PHONY: all clean run run-uefi run-hdd iso hdd debug verify-iso help

all: $(ISO_IMAGE) $(HDD_IMAGE)

# ---- Compilation rules ----

# Build user programs first
$(BUILD_DIR)/user/hello.elf $(BUILD_DIR)/user/cat.elf:
	@mkdir -p $(BUILD_DIR)/user
	$(MAKE) -C user

# Compile C files
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile assembly files
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Embed user programs as binary objects
$(BUILD_DIR)/user_hello.o: $(BUILD_DIR)/user/hello.elf
	@mkdir -p $(dir $@)
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

$(BUILD_DIR)/user_cat.o: $(BUILD_DIR)/user/cat.elf
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
               $(BUILD_DIR)/elf.o $(BUILD_DIR)/user_hello.o $(BUILD_DIR)/user_cat.o
	@mkdir -p $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $^

# ---- ISO image ----

$(ISO_DIR): $(KERNEL_ELF)
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
	# Boot images + UEFI
	cp $(LIMINE_DIR)/limine-bios-cd.bin  $(ISO_DIR)/boot/limine-bios-cd.bin
	cp $(LIMINE_DIR)/limine-bios-cd.bin  $(ISO_DIR)/boot/limine-cd.bin
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

# ---- HDD image (simpler, more reliable for development) ----

$(HDD_IMAGE): $(KERNEL_ELF)
	@echo "Building HDD image..."
	@rm -f $(HDD_IMAGE)
	# Create a 64MB raw disk image
	dd if=/dev/zero of=$(HDD_IMAGE) bs=1M count=64 status=none
	# Partition with single FAT32 partition
	parted -s $(HDD_IMAGE) mklabel msdos
	parted -s $(HDD_IMAGE) mkpart primary fat32 1MiB 100%
	parted -s $(HDD_IMAGE) set 1 boot on
	# Format as FAT32 (loop device with offset)
	@LOOPDEV=$$(sudo losetup -f --show -o 1048576 --sizelimit 66060288 $(HDD_IMAGE) 2>/dev/null || echo ""); \
	if [ -n "$$LOOPDEV" ]; then \
		sudo mkfs.fat -F 32 $$LOOPDEV >/dev/null 2>&1; \
		TMPDIR=$$(mktemp -d); \
		sudo mount $$LOOPDEV $$TMPDIR; \
		sudo mkdir -p $$TMPDIR/boot/limine; \
		sudo cp $(KERNEL_ELF) $$TMPDIR/boot/kernel.elf; \
		sudo cp $(LIMINE_CFG) $$TMPDIR/limine.conf; \
		sudo cp $(LIMINE_CFG) $$TMPDIR/boot/limine.conf; \
		sudo cp $(LIMINE_DIR)/limine-bios.sys $$TMPDIR/boot/limine/limine-bios.sys; \
		sudo cp $(LIMINE_DIR)/limine-bios.sys $$TMPDIR/limine-bios.sys; \
		sudo cp $(LIMINE_DIR)/limine-bios.sys $$TMPDIR/limine.sys; \
		sudo umount $$TMPDIR; \
		rmdir $$TMPDIR; \
		sudo losetup -d $$LOOPDEV; \
	else \
		echo "WARNING: Could not set up loop device. HDD image not created."; \
		rm -f $(HDD_IMAGE); \
	fi
	# Deploy Limine to MBR
	@if [ -f "$(HDD_IMAGE)" ]; then \
		if [ -f "$(LIMINE_DIR)/bin/limine-deploy" ]; then \
			$(LIMINE_DIR)/bin/limine-deploy bios-install $(HDD_IMAGE); \
		elif command -v limine-deploy >/dev/null 2>&1; then \
			limine-deploy bios-install $(HDD_IMAGE); \
		fi; \
		echo "HDD image built successfully: $(HDD_IMAGE)"; \
	fi

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
		$(QEMU) $(QEMU_FLAGS) -hda $(HDD_IMAGE) -no-reboot -no-shutdown; \
	else \
		echo "ERROR: HDD image not found. Run 'make hdd' first."; \
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