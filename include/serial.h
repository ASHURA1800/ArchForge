#ifndef ARCHFORGE_SERIAL_H
#define ARCHFORGE_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_write(const char *str);
void serial_write_char(char c);
void serial_write_hex(uint64_t value);
void serial_write_dec(uint64_t value);

#endif /* ARCHFORGE_SERIAL_H */
