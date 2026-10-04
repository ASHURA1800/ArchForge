/* Forwarding header — canonical serial.h is at include/serial.h.
 *
 * This file exists because kernel/ source files use `#include "serial.h"`,
 * which the compiler finds in the same directory before searching -I paths.
 * We forward to the canonical header to avoid code duplication.
 *
 * To remove this file safely:
 *   1. Change all `#include "serial.h"` to `#include <serial.h>`
 *      (the -I./include flag will find include/serial.h)
 *   2. Then delete this file.
 */
#include "../include/serial.h"
