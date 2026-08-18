#include "keyboard.h"

#include "ps2.h"
#include "../arch/x86_64/pic.h"

// Scancode set 1 -> character, indexed by make code (0x00-0x58).
// [0] = unshifted, [1] = shifted. 0 = key not mapped to a printable char.
// Caps lock is not handled yet.

// QWERTY (US) layout.
static const uint8_t qwerty_ascii[0x58][2] = {
    /* 0x00 */ {0, 0},          // error / key detection overrun
    /* 0x01 */ {0x1B, 0x1B},    // Esc
    /* 0x02 */ {'1', '!'},
    /* 0x03 */ {'2', '@'},
    /* 0x04 */ {'3', '#'},
    /* 0x05 */ {'4', '$'},
    /* 0x06 */ {'5', '%'},
    /* 0x07 */ {'6', '^'},
    /* 0x08 */ {'7', '&'},
    /* 0x09 */ {'8', '*'},
    /* 0x0A */ {'9', '('},
    /* 0x0B */ {'0', ')'},
    /* 0x0C */ {'-', '_'},
    /* 0x0D */ {'=', '+'},
    /* 0x0E */ {'\b', '\b'},    // Backspace
    /* 0x0F */ {'\t', '\t'},    // Tab
    /* 0x10 */ {'q', 'Q'},
    /* 0x11 */ {'w', 'W'},
    /* 0x12 */ {'e', 'E'},
    /* 0x13 */ {'r', 'R'},
    /* 0x14 */ {'t', 'T'},
    /* 0x15 */ {'y', 'Y'},
    /* 0x16 */ {'u', 'U'},
    /* 0x17 */ {'i', 'I'},
    /* 0x18 */ {'o', 'O'},
    /* 0x19 */ {'p', 'P'},
    /* 0x1A */ {'[', '{'},
    /* 0x1B */ {']', '}'},
    /* 0x1C */ {'\n', '\n'},    // Enter
    /* 0x1D */ {0, 0},          // Ctrl (state, not a key)
    /* 0x1E */ {'a', 'A'},
    /* 0x1F */ {'s', 'S'},
    /* 0x20 */ {'d', 'D'},
    /* 0x21 */ {'f', 'F'},
    /* 0x22 */ {'g', 'G'},
    /* 0x23 */ {'h', 'H'},
    /* 0x24 */ {'j', 'J'},
    /* 0x25 */ {'k', 'K'},
    /* 0x26 */ {'l', 'L'},
    /* 0x27 */ {';', ':'},
    /* 0x28 */ {'\'', '"'},
    /* 0x29 */ {'`', '~'},
    /* 0x2A */ {0, 0},          // Left shift (state, not a key)
    /* 0x2B */ {'\\', '|'},
    /* 0x2C */ {'z', 'Z'},
    /* 0x2D */ {'x', 'X'},
    /* 0x2E */ {'c', 'C'},
    /* 0x2F */ {'v', 'V'},
    /* 0x30 */ {'b', 'B'},
    /* 0x31 */ {'n', 'N'},
    /* 0x32 */ {'m', 'M'},
    /* 0x33 */ {',', '<'},
    /* 0x34 */ {'.', '>'},
    /* 0x35 */ {'/', '?'},
    /* 0x36 */ {0, 0},          // Right shift (state, not a key)
    /* 0x37 */ {0, 0},          // Keypad *
    /* 0x38 */ {0, 0},          // Alt (state, not a key)
    /* 0x39 */ {' ', ' '},      // Space
    /* 0x3A */ {0, 0},          // Caps lock
    /* 0x3B */ {0, 0},          // F1
    /* 0x3C */ {0, 0},          // F2
    /* 0x3D */ {0, 0},          // F3
    /* 0x3E */ {0, 0},          // F4
    /* 0x3F */ {0, 0},          // F5
    /* 0x40 */ {0, 0},          // F6
    /* 0x41 */ {0, 0},          // F7
    /* 0x42 */ {0, 0},          // F8
    /* 0x43 */ {0, 0},          // F9
    /* 0x44 */ {0, 0},          // F10
    /* 0x45 */ {0, 0},          // Num lock
    /* 0x46 */ {0, 0},          // Scroll lock
    /* 0x47 */ {0, 0},          // Keypad 7 / Home
    /* 0x48 */ {0, 0},          // Keypad 8 / Up
    /* 0x49 */ {0, 0},          // Keypad 9 / PgUp
    /* 0x4A */ {0, 0},          // Keypad -
    /* 0x4B */ {0, 0},          // Keypad 4 / Left
    /* 0x4C */ {0, 0},          // Keypad 5
    /* 0x4D */ {0, 0},          // Keypad 6 / Right
    /* 0x4E */ {0, 0},          // Keypad +
    /* 0x4F */ {0, 0},          // Keypad 1 / End
    /* 0x50 */ {0, 0},          // Keypad 2 / Down
    /* 0x51 */ {0, 0},          // Keypad 3 / PgDn
    /* 0x52 */ {0, 0},          // Keypad 0 / Ins
    /* 0x53 */ {0, 0},          // Keypad . / Del
    /* 0x56 */ {0, 0},          // Extra key (< > on 102-key)
    /* 0x57 */ {0, 0},          // F11
    /* 0x58 */ {0, 0},          // F12
};

// AZERTY (French) layout.
// French accented characters use the VGA code page 437 encoding:
//   é = 0x82   è = 0x8A   ç = 0x87   à = 0x85   ù = 0x97
//   ² = 0xFD   ° = 0xF8   £ = 0x9C   µ = 0xE6   § = 0x15   ¨ = 0xF9
// Dead keys (^ and ¨) are mapped to their plain characters: composing
// "ê" or "ë" is not implemented yet.
static const uint8_t azerty_ascii[0x58][2] = {
    /* 0x00 */ {0, 0},            // error / key detection overrun
    /* 0x01 */ {0x1B, 0x1B},      // Esc
    /* 0x02 */ {'&', '1'},
    /* 0x03 */ {'\x82', '2'},     // é
    /* 0x04 */ {'"', '3'},
    /* 0x05 */ {'\'', '4'},
    /* 0x06 */ {'(', '5'},
    /* 0x07 */ {'-', '6'},
    /* 0x08 */ {'\x8A', '7'},     // è
    /* 0x09 */ {'_', '8'},
    /* 0x0A */ {'\x87', '9'},     // ç
    /* 0x0B */ {'\x85', '0'},     // à
    /* 0x0C */ {')', '\xF8'},     // °
    /* 0x0D */ {'=', '+'},
    /* 0x0E */ {'\b', '\b'},      // Backspace
    /* 0x0F */ {'\t', '\t'},      // Tab
    /* 0x10 */ {'a', 'A'},
    /* 0x11 */ {'z', 'Z'},
    /* 0x12 */ {'e', 'E'},
    /* 0x13 */ {'r', 'R'},
    /* 0x14 */ {'t', 'T'},
    /* 0x15 */ {'y', 'Y'},
    /* 0x16 */ {'u', 'U'},
    /* 0x17 */ {'i', 'I'},
    /* 0x18 */ {'o', 'O'},
    /* 0x19 */ {'p', 'P'},
    /* 0x1A */ {'^', '\xF9'},     // ¨ (dead key, not composed)
    /* 0x1B */ {'$', '\x9C'},     // £
    /* 0x1C */ {'\n', '\n'},      // Enter
    /* 0x1D */ {0, 0},            // Ctrl (state, not a key)
    /* 0x1E */ {'q', 'Q'},
    /* 0x1F */ {'s', 'S'},
    /* 0x20 */ {'d', 'D'},
    /* 0x21 */ {'f', 'F'},
    /* 0x22 */ {'g', 'G'},
    /* 0x23 */ {'h', 'H'},
    /* 0x24 */ {'j', 'J'},
    /* 0x25 */ {'k', 'K'},
    /* 0x26 */ {'l', 'L'},
    /* 0x27 */ {'m', 'M'},
    /* 0x28 */ {'\x97', '%'},     // ù
    /* 0x29 */ {'\xFD', '\xFD'},  // ²
    /* 0x2A */ {0, 0},            // Left shift (state, not a key)
    /* 0x2B */ {'*', '\xE6'},     // µ
    /* 0x2C */ {'w', 'W'},
    /* 0x2D */ {'x', 'X'},
    /* 0x2E */ {'c', 'C'},
    /* 0x2F */ {'v', 'V'},
    /* 0x30 */ {'b', 'B'},
    /* 0x31 */ {'n', 'N'},
    /* 0x32 */ {',', '?'},
    /* 0x33 */ {';', '.'},
    /* 0x34 */ {':', '/'},
    /* 0x35 */ {'!', '\x15'},     // §
    /* 0x36 */ {0, 0},            // Right shift (state, not a key)
    /* 0x37 */ {0, 0},            // Keypad *
    /* 0x38 */ {0, 0},            // Alt (state, not a key)
    /* 0x39 */ {' ', ' '},        // Space
    /* 0x3A */ {0, 0},            // Caps lock
    /* 0x3B */ {0, 0},            // F1
    /* 0x3C */ {0, 0},            // F2
    /* 0x3D */ {0, 0},            // F3
    /* 0x3E */ {0, 0},            // F4
    /* 0x3F */ {0, 0},            // F5
    /* 0x40 */ {0, 0},            // F6
    /* 0x41 */ {0, 0},            // F7
    /* 0x42 */ {0, 0},            // F8
    /* 0x43 */ {0, 0},            // F9
    /* 0x44 */ {0, 0},            // F10
    /* 0x45 */ {0, 0},            // Num lock
    /* 0x46 */ {0, 0},            // Scroll lock
    /* 0x47 */ {0, 0},            // Keypad 7 / Home
    /* 0x48 */ {0, 0},            // Keypad 8 / Up
    /* 0x49 */ {0, 0},            // Keypad 9 / PgUp
    /* 0x4A */ {0, 0},            // Keypad -
    /* 0x4B */ {0, 0},            // Keypad 4 / Left
    /* 0x4C */ {0, 0},            // Keypad 5
    /* 0x4D */ {0, 0},            // Keypad 6 / Right
    /* 0x4E */ {0, 0},            // Keypad +
    /* 0x4F */ {0, 0},            // Keypad 1 / End
    /* 0x50 */ {0, 0},            // Keypad 2 / Down
    /* 0x51 */ {0, 0},            // Keypad 3 / PgDn
    /* 0x52 */ {0, 0},            // Keypad 0 / Ins
    /* 0x53 */ {0, 0},            // Keypad . / Del
    /* 0x56 */ {'<', '>'},        // Extra key (<> on AZERTY)
    /* 0x57 */ {0, 0},            // F11
    /* 0x58 */ {0, 0},            // F12
};

// Active layout table. Defaults to AZERTY (French keyboard).
static const uint8_t (*active_layout)[2] = azerty_ascii;

// Pick which layout keyboard_scancode_to_ascii uses.
void keyboard_set_layout(enum keyboard_layout layout)
{
    if (layout == KEYBOARD_LAYOUT_QWERTY)
        active_layout = qwerty_ascii;
    else
        active_layout = azerty_ascii;
}

void keyboard_init(void)
{
    // Initialize the PS/2 controller and reset the keyboard device
    // (disable ports, flush buffer, self tests, re-enable, device reset).
    ps2_init();

    // Tell the keyboard to start sending scancodes. It answers ACK (0xFA):
    // drain it so the IRQ handler doesn't mistake it for a scancode.
    ps2_write_data(PS2_DEVICE_CMD_ENABLE);
    ps2_read_data();

    // Keyboard is wired to IRQ1: let its interrupts through.
    irq_unmask(1);
}

// Read one scancode from the keyboard (PS/2 output buffer, port 0x60).
// Called from the IRQ1 handler once the interrupt fires.
uint8_t keyboard_get_scancode(void)
{
    return ps2_read_data();
}

// Translate a set-1 scancode into its ASCII character.
// Shift state is tracked internally; break codes and unmapped keys return 0.
char keyboard_scancode_to_ascii(uint8_t scancode)
{
    static uint8_t shift_pressed = 0;

    // Break code (bit 7 set): key released. Track shift releases,
    // ignore the rest.
    if (scancode & 0x80) {
        scancode &= 0x7F;
        if (scancode == 0x2A || scancode == 0x36)
            shift_pressed = 0;
        return 0;
    }

    // Left/right shift pressed: remember it, nothing to print.
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return 0;
    }

    // Extended keys (0xE0 prefix and beyond) aren't mapped yet.
    if (scancode >= 0x58)
        return 0;

    return active_layout[scancode][shift_pressed];
}
