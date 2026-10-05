#ifndef _TIMER_H
#define _TIMER_H

#include <stdint.h>

void timer_init(uint32_t frequency);
void timer_irq_handler(void);
uint64_t timer_get_ticks(void);
uint64_t timer_get_ms(void);
void timer_sleep_ms(uint64_t ms);

#define timer_ticks() timer_get_ticks()

#endif