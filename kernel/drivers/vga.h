#ifndef KERNEL_VGA_H
#define KERNEL_VGA_H

#include "../lib/stdint.h"
#include "../lib/stddef.h"

#define VGA_BUFFER ((volatile uint16_t *)0xB8000)

#define VGA_CTRL_REGISTER 0x3D4
#define VGA_DATA_REGISTER 0x3D5

#define VGA_KERNEL_HEART_WIDTH  40
#define VGA_KERNEL_HEART_HEIGHT 20
#define VGA_KERNEL_HEART_SCALE  1000

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

typedef struct  {
    uint8_t x; // 80 max
    uint8_t y; // 25 max
} vga_pos_t;

enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE,
    VGA_COLOR_GREEN,
    VGA_COLOR_CYAN,
    VGA_COLOR_RED,
    VGA_COLOR_MAGENTA,
    VGA_COLOR_BROWN,
    VGA_COLOR_LIGHT_GRAY,
    VGA_COLOR_DARK_GRAY,
    VGA_COLOR_LIGHT_BLUE,
    VGA_COLOR_LIGHT_GREEN,
    VGA_COLOR_LIGHT_CYAN,
    VGA_COLOR_LIGHT_RED,
    VGA_COLOR_LIGHT_MAGENTA,
    VGA_COLOR_LIGHT_BROWN,
    VGA_COLOR_WHITE, // 15
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

void vga_puthex(uint64_t value);

void vga_clear(void);
void vga_init(void);

// kernel logo
void vga_draw_heart(uint8_t start_x, uint8_t start_y);

#endif // KERNEL_VGA_H
