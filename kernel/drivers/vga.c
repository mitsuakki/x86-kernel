#include "vga_text.h"

static volatile unsigned int vga_cursor = 0;

void putc(char c, enum vga_color color)
{
    if (vga_cursor >= VGA_WIDTH * VGA_HEIGHT)
        return;

    VGA_BUFFER[vga_cursor++] = c | (color << 8);
}

void write(const char *str, enum vga_color color)
{
    while (*str) {
        putc(*str, color);
        str++;
    }
}

void clear_screen()
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_BUFFER[i] = ' ' | (COLOR_BLACK << 8);
    }

    vga_cursor = 0;
}
