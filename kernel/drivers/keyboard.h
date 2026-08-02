#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

#include "../lib/stdint.h"

void keyboard_init(void);

uint8_t keyboard_get_scancode(void);
char keyboard_scancode_to_ascii(uint8_t scancode);

#endif // KERNEL_KEYBOARD_H
