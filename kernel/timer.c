/* kernel/timer.c — Programmable Interval Timer (PIT) driver
 *
 * The PIT is connected to IRQ0. We program it to generate interrupts
 * at a specific frequency, which we use for timekeeping and sleeping.
 */
#include "timer.h"
#include "io.h"
#include "serial.h"

static volatile uint64_t timer_ticks = 0;
static uint32_t timer_frequency = 0;

/* PIT I/O ports */
#define PIT_COMMAND_PORT  0x43
#define PIT_CHANNEL0_PORT 0x40

/* PIT base frequency (Hz) */
#define PIT_BASE_FREQUENCY 1193182

void timer_init(uint32_t frequency) {
    timer_frequency = frequency;
    uint16_t divisor = (uint16_t)(PIT_BASE_FREQUENCY / frequency);

    serial_write("[TIMER] Initializing PIT at ");
    serial_write_dec(frequency);
    serial_write(" Hz (divisor: ");
    serial_write_dec(divisor);
    serial_write(")\n");

    /* Send command byte: Channel 0, LSB then MSB, Mode 3 (Square wave generator) */
    outb(PIT_COMMAND_PORT, 0x36);

    /* Send divisor: LSB first, then MSB */
    outb(PIT_CHANNEL0_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_PORT, (uint8_t)((divisor >> 8) & 0xFF));

    serial_write("[TIMER] PIT initialized successfully.\n");
}

void timer_irq_handler(void) {
    timer_ticks++;
    /* Note: EOI is sent by the caller in interrupt_handlers.c */
}

uint64_t timer_get_ticks(void) {
    return timer_ticks;
}

uint64_t timer_get_ms(void) {
    if (timer_frequency == 0) return 0;
    return (timer_ticks * 1000) / timer_frequency;
}

void timer_sleep_ms(uint64_t ms) {
    if (timer_frequency == 0) return;
    
    uint64_t target_ticks = timer_ticks + ((ms * timer_frequency) / 1000);
    while (timer_ticks < target_ticks) {
        /* Halt CPU to save power while waiting for next interrupt */
        __asm__ volatile("hlt");
    }
}