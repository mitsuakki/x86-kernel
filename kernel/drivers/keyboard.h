#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

#include "ps2.h"
#include "../lib/stdint.h"

// Keyboard-specific commands (sent to device via PS2_DATA_PORT)
#define KEYBOARD_CMD_SET_LED          0xED
#define KEYBOARD_CMD_LED_SCROLLL_LOCK (1 << 0)
#define KEYBOARD_CMD_LED_NUMBER_LOCK  (1 << 1)
#define KEYBOARD_CMD_LED_CAPS_LOCK    (1 << 2)
#define KEYBOARD_CMD_LED_KANA         (1 << 4) // Japanese keyboard (bit 4)

#define KEYBOARD_CMD_ECHO 0xEE // Echo (for diagnostics, device removal detection)

#define KEYBOARD_CMD_SCANCODE                    0xF0 // Get/set current scan code set
#define KEYBOARD_CMD_SCANCODE_SUB_GET            0x00 // Get current scan code set
#define KEYBOARD_CMD_SCANCODE_SUB_SET_1          0x01 // Set scan code set 1
#define KEYBOARD_CMD_SCANCODE_SUB_SET_2          0x02 // Set scan code set 2
#define KEYBOARD_CMD_SCANCODE_SUB_SET_3          0x03 // Set scan code set 3
#define KEYBOARD_CMD_SCANCODE_SUB_RAW_1          0x01 // Raw ID: scan code set 1
#define KEYBOARD_CMD_SCANCODE_SUB_RAW_2          0x02 // Raw ID: scan code set 2
#define KEYBOARD_CMD_SCANCODE_SUB_RAW_3          0x03 // Raw ID: scan code set 3
#define KEYBOARD_CMD_SCANCODE_SUB_ID_TRANSLATE_1 0x43 // Translated ID: scan code set 1
#define KEYBOARD_CMD_SCANCODE_SUB_ID_TRANSLATE_2 0x41 // Translated ID: scan code set 2
#define KEYBOARD_CMD_SCANCODE_SUB_ID_TRANSLATE_3 0x3F // Translated ID: scan code set 3

#define KEYBOARD_CMD_TYPEMATIC            0xF3 // Set typematic rate and delay
#define KEYBOARD_CMD_TYPEMATIC_RATE_MASK  0x1F // bits 0-4: 00000b = 30 Hz ... 11111b = 2 Hz
#define KEYBOARD_CMD_TYPEMATIC_DELAY_250  0x00 // bits 5-6: 00b = 250 ms
#define KEYBOARD_CMD_TYPEMATIC_DELAY_500  0x20 // bits 5-6: 01b = 500 ms
#define KEYBOARD_CMD_TYPEMATIC_DELAY_750  0x40 // bits 5-6: 10b = 750 ms
#define KEYBOARD_CMD_TYPEMATIC_DELAY_1000 0x60 // bits 5-6: 11b = 1000 ms
#define KEYBOARD_CMD_TYPEMATIC_ZERO_BIT7  (1 << 7) // MUST be 0

#define KEYBOARD_CMD_SET_DEFAULTS_PARAMETERS 0xF6 // Set default parameters

#define KEYBOARD_CMD_ALL_TYPEMATIC       0xF7 // Set all keys to typematic/autorepeat only (scancode set 3 only)
#define KEYBOARD_CMD_ALL_MAKE_RELEASE    0xF8 // Set all keys to make/release (scancode set 3 only)
#define KEYBOARD_CMD_ALL_MAKE_ONLY       0xF9 // Set all keys to make only (scancode set 3 only)
#define KEYBOARD_CMD_ALL_BREAK_TYPEMATIC 0xFA // Set all keys to typematic/autorepeat/make/release (scancode set 3 only)

#define KEYBOARD_CMD_KEY_TYPEMATIC    0xFB // Set specific key to typematic/autorepeat only (scancode set 3 only)
#define KEYBOARD_CMD_KEY_MAKE_RELEASE 0xFC // Set specific key to make/release (scancode set 3 only)
#define KEYBOARD_CMD_KEY_MAKE_ONLY    0xFD // Set specific key to make only (scancode set 3 only)

#define KEYBOARD_CMD_RESEND 0xFE // Resend last byte

// Special bytes the keyboard may send (not scan codes)
#define KEYBOARD_SPECIAL_OVERFLOW_ERR1   0x00 // Key detection error or internal buffer overrun
#define KEYBOARD_SPECIAL_OVERFLOW_ERR2   0xFF // Key detection error or internal buffer overrun
#define KEYBOARD_SPECIAL_ECHO            0xEE // Response to echo command

// Keyboard layouts: same physical scancodes, different printed characters.
// QWERTY = US layout, AZERTY = French layout.
enum keyboard_layout {
    KEYBOARD_LAYOUT_QWERTY = 0,
    KEYBOARD_LAYOUT_AZERTY,
};

void keyboard_init(void);

void keyboard_set_layout(enum keyboard_layout layout);

uint8_t keyboard_get_scancode(void);
char keyboard_scancode_to_ascii(uint8_t scancode);

#endif // KERNEL_KEYBOARD_H
