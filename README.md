# ArchForge OS

A custom 64-bit x86_64 operating system built from scratch.

## Project Structure

```
ArchForge/
├── boot/               # Bootloader configuration (limine.cfg)
├── build/              # Generated build artifacts (gitignored)
├── include/            # Header files
│   ├── io.h            # Port I/O functions
│   ├── limine.h        # Limine bootloader protocol definitions
│   └── serial.h        # Serial port output functions
├── kernel/             # Kernel source code
│   ├── kernel.c        # Main kernel entry point
│   ├── linker.ld       # Linker script for x86_64 freestanding kernel
│   ├── serial.c        # Serial port implementation
│   └── start.S         # Assembly entry point and stack setup
├── scripts/            # Build and utility scripts
│   └── download_limine.sh # Script to download Limine binaries
├── Makefile            # Build system configuration
└── README.md           # This file
```

## Required Dependencies

To build and run ArchForge OS on EndeavourOS (or any Arch-based distribution), you need the following packages:

```bash
sudo pacman -S clang lld nasm xorriso qemu-base gdb limine
```

*Note: If the `limine` package is not available or you prefer a specific version, you can run `./scripts/download_limine.sh` to download prebuilt Limine binaries.*

## How to Build

1. Clone or navigate to the project directory:
   ```bash
   cd ~/Projects/ArchForge
   ```

2. Build the kernel and bootable ISO:
   ```bash
   make
   ```

   This will:
   - Compile the assembly and C source files with freestanding x86_64 flags.
   - Link the objects using the custom linker script.
   - Deploy the Limine bootloader.
   - Generate a bootable ISO image at `build/archforge.iso`.

3. To clean all generated artifacts:
   ```bash
   make clean
   ```

## How to Run in QEMU

To launch the OS in QEMU with serial output redirected to your terminal:

```bash
make run
```

You should see the following output in your terminal:
```
Welcome to ArchForge OS
Framebuffer initialized: 0x0000000000000400x0x0000000000000300 @ 0x0000000000000020 bpp
```
*(Note: Hex values represent width, height, and bits per pixel, e.g., 1024x768 @ 32 bpp)*

### Debugging with GDB

To debug the kernel, run:
```bash
make debug
```
Then, in another terminal, connect with GDB:
```bash
gdb build/kernel.elf
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
```

## Known Limitations

- **No multitasking**: The kernel currently runs a single thread and halts after initialization.
- **No memory management**: Physical and virtual memory management are not yet implemented.
- **No filesystem**: The kernel cannot read files from the ISO beyond what the bootloader provides.
- **Basic output**: Only serial output and a solid-color framebuffer clear are implemented. No text-mode framebuffer driver yet.
- **No interrupt handling**: IDT and PIC/APIC are not configured.

## Next Development Milestone

- **Phase 2**: Implement a basic text-mode framebuffer driver to print directly to the screen, and add basic keyboard input handling (PS/2).

## License

This project is for educational and experimental purposes.