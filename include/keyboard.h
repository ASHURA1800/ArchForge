#ifndef ARCHFORGE_KEYBOARD_H
#define ARCHFORGE_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize the PS/2 keyboard driver */
void keyboard_init(void);

/* Check if a key is available in the buffer */
bool keyboard_has_key(void);

/* Get the next key from the buffer (blocking) */
char keyboard_getc(void);

/* Get the next key from the buffer (non-blocking) */
char keyboard_getc_nb(void);

/* Keyboard interrupt handler - called from IRQ1 handler */
void keyboard_irq_handler(void);

#endif /* ARCHFORGE_KEYBOARD_H */