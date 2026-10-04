/* kernel/keyboard.c — PS/2 Keyboard Driver
 *
 * Handles IRQ1 from the PS/2 keyboard controller.
 * Translates scancodes to ASCII characters.
 * Supports shift, caps lock, and extended keys.
 */
#include "keyboard.h"
#include "serial.h"
#include "idt.h"
#include "io.h"
#include "../include/stdbool.h"
#include "../include/stddef.h"

/* Keyboard controller ports */
#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64
#define KBD_CMD_PORT    0x64

/* Keyboard commands */
#define KBD_CMD_SET_LEDS    0xED
#define KBD_CMD_ENABLE      0xF4
#define KBD_CMD_RESET       0xFF

/* Keyboard status register bits */
#define KBD_STATUS_OBF  0x01  /* Output buffer full */
#define KBD_STATUS_IBF  0x02  /* Input buffer full */
#define KBD_STATUS_SYS  0x04  /* System flag */
#define KBD_STATUS_CMD  0x08  /* Command/data */
#define KBD_STATUS_AUX  0x20  /* Auxiliary device (mouse) */

/* Scancode set 1 (XT) - make codes */
#define SC_ESC        0x01
#define SC_1          0x02
#define SC_2          0x03
#define SC_3          0x04
#define SC_4          0x05
#define SC_5          0x06
#define SC_6          0x07
#define SC_7          0x08
#define SC_8          0x09
#define SC_9          0x0A
#define SC_0          0x0B
#define SC_MINUS      0x0C
#define SC_EQUALS     0x0D
#define SC_BACKSPACE  0x0E
#define SC_TAB        0x0F
#define SC_Q          0x10
#define SC_W          0x11
#define SC_E          0x12
#define SC_R          0x13
#define SC_T          0x14
#define SC_Y          0x15
#define SC_U          0x16
#define SC_I          0x17
#define SC_O          0x18
#define SC_P          0x19
#define SC_LBRACKET   0x1A
#define SC_RBRACKET   0x1B
#define SC_ENTER      0x1C
#define SC_LCTRL      0x1D
#define SC_A          0x1E
#define SC_S          0x1F
#define SC_D          0x20
#define SC_F          0x21
#define SC_G          0x22
#define SC_H          0x23
#define SC_J          0x24
#define SC_K          0x25
#define SC_L          0x26
#define SC_SEMICOLON  0x27
#define SC_APOSTROPHE 0x28
#define SC_GRAVE      0x29
#define SC_LSHIFT     0x2A
#define SC_BACKSLASH  0x2B
#define SC_Z          0x2C
#define SC_X          0x2D
#define SC_C          0x2E
#define SC_V          0x2F
#define SC_B          0x30
#define SC_N          0x31
#define SC_M          0x32
#define SC_COMMA      0x33
#define SC_DOT        0x34
#define SC_SLASH      0x35
#define SC_RSHIFT     0x36
#define SC_KP_ASTERISK 0x37
#define SC_LALT       0x38
#define SC_SPACE      0x39
#define SC_CAPSLOCK   0x3A
#define SC_F1         0x3B
#define SC_F2         0x3C
#define SC_F3         0x3D
#define SC_F4         0x3E
#define SC_F5         0x3F
#define SC_F6         0x40
#define SC_F7         0x41
#define SC_F8         0x42
#define SC_F9         0x43
#define SC_F10        0x44
#define SC_NUMLOCK    0x45
#define SC_SCROLLLOCK 0x46
#define SC_KP_7       0x47
#define SC_KP_8       0x48
#define SC_KP_9       0x49
#define SC_KP_MINUS   0x4A
#define SC_KP_4       0x4B
#define SC_KP_5       0x4C
#define SC_KP_6       0x4D
#define SC_KP_PLUS    0x4E
#define SC_KP_1       0x4F
#define SC_KP_2       0x50
#define SC_KP_3       0x51
#define SC_KP_0       0x52
#define SC_KP_DOT     0x53

/* Extended scancodes (E0 prefix) */
#define SC_KP_ENTER   0x1C
#define SC_RCTRL      0x1D
#define SC_KP_SLASH   0x35
#define SC_RALT       0x38
#define SC_HOME       0x47
#define SC_UP         0x48
#define SC_PGUP       0x49
#define SC_LEFT       0x4B
#define SC_RIGHT      0x4D
#define SC_END        0x4F
#define SC_DOWN       0x50
#define SC_PGDN       0x51
#define SC_INSERT     0x52
#define SC_DELETE     0x53

/* Break code prefix */
#define SC_BREAK      0x80
#define SC_E0         0xE0

/* Key state */
static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool capslock_on = false;
static bool numlock_on = false;
static bool scrolllock_on = false;
static bool extended = false;

/* Key buffer */
#define KEY_BUFFER_SIZE 256
static char key_buffer[KEY_BUFFER_SIZE];
static size_t buffer_head = 0;
static size_t buffer_tail = 0;

/* Scancode to ASCII (unshifted) */
static const char scancode_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+',
    '1', '2', '3', '0', '.'
};

/* Scancode to ASCII (shifted) */
static const char scancode_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+',
    '1', '2', '3', '0', '.'
};

/* Wait for keyboard controller input buffer to be empty */
static void kbd_wait_ibf(void) {
    while (inb(KBD_STATUS_PORT) & KBD_STATUS_IBF) {
        /* Wait */
    }
}

/* Wait for keyboard controller output buffer to be full */
static void kbd_wait_obf(void) {
    while (!(inb(KBD_STATUS_PORT) & KBD_STATUS_OBF)) {
        /* Wait */
    }
}

/* Send command to keyboard controller */
static void kbd_send_cmd(uint8_t cmd) {
    kbd_wait_ibf();
    outb(KBD_CMD_PORT, cmd);
}

/* Send data to keyboard */
static void kbd_send_data(uint8_t data) {
    kbd_wait_ibf();
    outb(KBD_DATA_PORT, data);
}

/* Read data from keyboard */
static uint8_t kbd_read_data(void) {
    kbd_wait_obf();
    return inb(KBD_DATA_PORT);
}

/* Add character to key buffer */
static void key_buffer_put(char c) {
    size_t next = (buffer_head + 1) % KEY_BUFFER_SIZE;
    if (next != buffer_tail) {
        key_buffer[buffer_head] = c;
        buffer_head = next;
    }
}

/* ====================================================================
 * keyboard_init — Initialize PS/2 keyboard
 * ==================================================================== */
void keyboard_init(void) {
    serial_write("[KEYBOARD] Initializing PS/2 keyboard...\n");

    /* Disable keyboard and mouse */
    kbd_send_cmd(0xAD);  /* Disable keyboard */
    kbd_send_cmd(0xA7);  /* Disable mouse */

    /* Flush output buffer */
    while (inb(KBD_STATUS_PORT) & KBD_STATUS_OBF) {
        inb(KBD_DATA_PORT);
    }

    /* Set keyboard to scancode set 1 */
    kbd_send_data(0xF0);  /* Set scancode set */
    kbd_read_data();      /* ACK */
    kbd_send_data(0x01);  /* Set 1 */
    kbd_read_data();      /* ACK */

    /* Enable keyboard */
    kbd_send_data(0xF4);  /* Enable scanning */
    kbd_read_data();      /* ACK */

    /* Enable keyboard */
    kbd_send_cmd(0xAE);   /* Enable keyboard */

    /* Enable IRQ1 (keyboard) on PIC */
    uint8_t mask = inb(0x21);
    mask &= ~0x02;  /* Unmask IRQ1 */
    outb(0x21, mask);

    serial_write("[KEYBOARD] Keyboard initialized (scancode set 1, IRQ1 enabled)\n");
}

/* Keyboard interrupt handler - called from IRQ1 handler */
void keyboard_irq_handler(void) {
    uint8_t scancode = inb(KBD_DATA_PORT);

    /* Handle extended scancode prefix */
    if (scancode == SC_E0) {
        extended = true;
        return;
    }

    /* Check for break code */
    bool break_code = scancode & SC_BREAK;
    uint8_t make_code = scancode & 0x7F;

    /* Handle modifier keys - check extended flag for right-side modifiers */
    if (extended) {
        switch (make_code) {
            case SC_RCTRL:   /* E0 1D */
                ctrl_pressed = !break_code;
                break;
            case SC_RALT:    /* E0 38 */
                alt_pressed = !break_code;
                break;
            case SC_KP_ENTER: /* E0 1C */
                if (!break_code) key_buffer_put('\n');
                break;
            case SC_KP_SLASH: /* E0 35 */
                if (!break_code) key_buffer_put('/');
                break;
            case SC_HOME:    /* E0 47 */
            case SC_UP:      /* E0 48 */
            case SC_PGUP:    /* E0 49 */
            case SC_LEFT:    /* E0 4B */
            case SC_RIGHT:   /* E0 4D */
            case SC_END:     /* E0 4F */
            case SC_DOWN:    /* E0 50 */
            case SC_PGDN:    /* E0 51 */
            case SC_INSERT:  /* E0 52 */
            case SC_DELETE:  /* E0 53 */
                /* Arrow keys and navigation - could be handled here */
                break;
        }
        extended = false;
        outb(0x20, 0x20); /* EOI */
        return;
    }

    /* Non-extended modifier keys */
    switch (make_code) {
        case SC_LSHIFT:
        case SC_RSHIFT:
            shift_pressed = !break_code;
            break;
        case SC_LCTRL:    /* 1D */
            ctrl_pressed = !break_code;
            break;
        case SC_LALT:     /* 38 */
            alt_pressed = !break_code;
            break;
        case SC_CAPSLOCK:
            if (!break_code) {
                capslock_on = !capslock_on;
            }
            break;
        case SC_NUMLOCK:
            if (!break_code) {
                numlock_on = !numlock_on;
            }
            break;
        case SC_SCROLLLOCK:
            if (!break_code) {
                scrolllock_on = !scrolllock_on;
            }
            break;
    }

    /* Handle printable keys on make (not break) */
    if (!break_code && make_code < 128) {
        char c = 0;

        if (shift_pressed ^ capslock_on) {
            c = scancode_shift[make_code];
        } else {
            c = scancode_ascii[make_code];
        }

        if (c) {
            key_buffer_put(c);
        }
    }

    extended = false;

    /* Send EOI to PIC */
    outb(0x20, 0x20);
}

/* ====================================================================
 * keyboard_has_key — Check if key is available
 * ==================================================================== */
bool keyboard_has_key(void) {
    return buffer_head != buffer_tail;
}

/* ====================================================================
 * keyboard_getc — Get next key (blocking)
 * ==================================================================== */
char keyboard_getc(void) {
    while (!keyboard_has_key()) {
        __asm__ volatile("hlt");
    }
    char c = key_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEY_BUFFER_SIZE;
    return c;
}

/* ====================================================================
 * keyboard_getc_nb — Get next key (non-blocking)
 * ==================================================================== */
char keyboard_getc_nb(void) {
    if (!keyboard_has_key()) return 0;
    char c = key_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEY_BUFFER_SIZE;
    return c;
}