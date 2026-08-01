#ifndef VGA_H
#define VGA_H

#include "../lib/stdint.h"
#include "../lib/stddef.h"

#define VGA_BUFFER ((volatile uint16_t *)0xB8000)

#define VGA_CTRL_REGISTER 0x3D4
#define VGA_DATA_REGISTER 0x3D5

static const size_t VGA_WIDTH  = 80;
static const size_t VGA_HEIGHT = 25;

typedef struct  {
    uint8_t x; // 80
    uint8_t y; // 25
} vga_pos_t;

enum vga_color {
    COLOR_BLACK = 0,
    COLOR_BLUE,
    COLOR_GREEN,
    COLOR_CYAN,
    COLOR_RED,
    COLOR_MAGENTA,
    COLOR_BROWN,
    COLOR_LIGHT_GRAY,
    COLOR_DARK_GRAY,
    COLOR_LIGHT_BLUE,
    COLOR_LIGHT_GREEN,
    COLOR_LIGHT_CYAN,
    COLOR_LIGHT_RED,
    COLOR_LIGHT_MAGENTA,
    COLOR_LIGHT_BROWN,
    COLOR_WHITE, // 15
};

uint8_t  vga_make_color(enum vga_color fg, enum vga_color bg);
uint16_t vga_entry(char c, uint8_t attr);

void vga_set_color(enum vga_color fg, enum vga_color bg);
uint8_t vga_get_color(void);

void vga_set_cursor(vga_pos_t pos);
vga_pos_t vga_get_cursor(void);

void vga_putchar(const char c);
void vga_putchar_at(const char c, vga_pos_t pos);
void vga_puts(const char* str);

void vga_clear(void);
void vga_init(void);

#endif // VGA_H
