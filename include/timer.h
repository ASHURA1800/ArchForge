#ifndef ARCHFORGE_TIMER_H
#define ARCHFORGE_TIMER_H

#include <stdint.h>

/* Initialize PIT (Programmable Interval Timer) at the given frequency (Hz) */
void timer_init(uint32_t frequency);

/* Get milliseconds since boot */
uint64_t timer_get_ms(void);

/* Get ticks since boot */
uint64_t timer_get_ticks(void);

/* Sleep for the given number of milliseconds */
void timer_sleep_ms(uint64_t ms);

/* IRQ handler called from interrupt_handlers.c (to be connected later) */
void timer_irq_handler(void);

#endif /* ARCHFORGE_TIMER_H */