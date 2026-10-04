/* kernel/serial.c — COM1 (0x3F8) serial driver
 * Provides basic serial output for kernel debugging.
 * COM1 is at I/O port 0x3F8.
 */
#include "serial.h"
#include "../include/io.h"

#define COM1 0x3F8

/* ====================================================================
 * serial_init — Initialize COM1 to 38400 baud, 8N1
 * ==================================================================== */
void serial_init(void) {
    /* Disable all interrupts */
    outb(COM1 + 1, 0x00);

    /* Enable DLAB (set baud rate divisor) */
    outb(COM1 + 3, 0x80);

    /* Set divisor to 3 (lo byte) -> 38400 baud
     * Divisor = 115200 / baud_rate. For 38400: 115200/38400 = 3 */
    outb(COM1 + 0, 0x03);  /* Divisor low byte  */
    outb(COM1 + 1, 0x00);  /* Divisor high byte */

    /* 8 bits, no parity, one stop bit (8N1), disable DLAB */
    outb(COM1 + 3, 0x03);

    /* Enable FIFO, clear them, with 14-byte threshold */
    outb(COM1 + 2, 0xC7);

    /* MCR: DTR + RTS + OUT2 (OUT2 required for interrupts on PC)
     * 0x0F = 00001111b = DTR + RTS + OUT1 + OUT2
     * We use 0x0F (not 0x0B) to ensure OUT1 and OUT2 are both set,
     * which is the most permissive configuration. */
    outb(COM1 + 4, 0x0F);
}

/* ====================================================================
 * serial_tx_ready — Wait for the transmit holding register to be empty.
 * Returns 1 if ready, 0 on timeout.
 * ==================================================================== */
static int serial_tx_ready(void) {
    /* LSR (Line Status Register) at COM1+5, bit 5 = THR empty */
    for (volatile int i = 0; i < 1000000; i++) {
        if (inb(COM1 + 5) & 0x20) return 1;
    }
    /* If we can't confirm TX ready, just proceed anyway.
     * Writing to the data register when not ready may lose a char,
     * but won't hang the kernel. */
    return 0;
}

/* ====================================================================
 * serial_write_char — Write a single character to COM1
 * ==================================================================== */
void serial_write_char(char c) {
    serial_tx_ready();
    outb(COM1, (uint8_t)c);
}

/* ====================================================================
 * serial_write — Write a null-terminated string to COM1
 * Automatically converts \n to \r\n for proper terminal display.
 * ==================================================================== */
void serial_write(const char *str) {
    if (str == (const char *)0) return;
    while (*str) {
        if (*str == '\n') {
            serial_tx_ready();
            outb(COM1, '\r');   /* Send CR before LF for proper newline */
        }
        serial_tx_ready();
        outb(COM1, (uint8_t)*str);
        str++;
    }
}

/* ====================================================================
 * serial_write_hex — Write a 64-bit value in hexadecimal (0x...)
 * ==================================================================== */
void serial_write_hex(uint64_t value) {
    const char *hex_digits = "0123456789abcdef";
    char buf[19]; /* "0x" + 16 hex digits + null */
    int pos = 18;
    buf[pos] = '\0';

    if (value == 0) {
        serial_write("0x0");
        return;
    }

    while (value > 0 && pos > 2) {
        buf[--pos] = hex_digits[value & 0xF];
        value >>= 4;
    }
    buf[--pos] = 'x';
    buf[--pos] = '0';

    serial_write(&buf[pos]);
}

/* ====================================================================
 * serial_write_dec — Write a 64-bit value in decimal
 * ==================================================================== */
void serial_write_dec(uint64_t value) {
    char buf[21];
    int pos = 20;
    buf[pos] = '\0';

    if (value == 0) {
        serial_write("0");
        return;
    }

    while (value > 0 && pos > 0) {
        buf[--pos] = '0' + (value % 10);
        value /= 10;
    }

    serial_write(&buf[pos]);
}
