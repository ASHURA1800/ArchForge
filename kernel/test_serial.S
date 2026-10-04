/* DEPRECATED - Minimal serial test, superseded by kernel/start.S beacons.
 *
 * This file previously contained a standalone test kernel with its own
 * _start symbol. It is NOT compiled by the Makefile (not listed in
 * KERNEL_ELF dependencies), so it has no effect on the build.
 *
 * The serial-output test is now integrated into kernel/start.S, which
 * prints beacons 'S', 't', 'G' at each stage of the boot sequence.
 *
 * To remove: rm /home/sagar/Projects/ArchForge/kernel/test_serial.S
 */
