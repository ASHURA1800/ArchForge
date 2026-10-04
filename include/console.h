#ifndef ARCHFORGE_CONSOLE_H
#define ARCHFORGE_CONSOLE_H

#include <stdint.h>
#include <stddef.h>

/* Initialize the framebuffer console */
void console_init(void);

/* Write a single character to the console */
void console_putc(char c);

/* Write a string to the console */
void console_write(const char *str);

/* Write a hex value to the console */
void console_write_hex(uint64_t value);

/* Write a decimal value to the console */
void console_write_dec(uint64_t value);

/* Clear the screen */
void console_clear(void);

/* Set cursor position */
void console_set_cursor(size_t row, size_t col);

/* Get cursor position */
void console_get_cursor(size_t *row, size_t *col);

/* Scroll the screen up by one line */
void console_scroll(void);

#endif /* ARCHFORGE_CONSOLE_H */