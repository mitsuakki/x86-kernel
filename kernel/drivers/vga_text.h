#ifndef VGA_TEXT_H
#define VGA_TEXT_H

#include "../lib/stdint.h"

#define VGA_BUFFER ((volatile uint16_t *)0xB8000)
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

enum vga_color
{
    COLOR_BLACK = 0x0,
    COLOR_BLUE = 0x1,
    COLOR_GREEN = 0x2,
    COLOR_CYAN = 0x3,
    COLOR_RED = 0x4,
    COLOR_MAGENTA = 0x5,
    COLOR_BROWN = 0x6,
    COLOR_LIGHT_GRAY = 0x7,
    COLOR_DARK_GRAY = 0x8,
    COLOR_LIGHT_BLUE = 0x9,
    COLOR_LIGHT_GREEN = 0xA,
    COLOR_LIGHT_CYAN = 0xB,
    COLOR_LIGHT_RED = 0xC,
    COLOR_LIGHT_MAGENTA = 0xD,
    COLOR_LIGHT_BROWN = 0xE,
    COLOR_WHITE = 0xF,
};

void putc(char c, enum vga_color color);
void write(const char *str, enum vga_color color);

void clear_screen();

#endif // VGA_TEXT_H
