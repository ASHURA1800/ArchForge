#ifndef FONT_H
#define FONT_H

#include "stdint.h"

// Public-domain 8x16 VGA font (256 glyphs, 16 bytes each = 4096 bytes total)
extern const uint8_t font_8x16_data[4096];

#endif // FONT_H
