#include "vga.h"

static uint8_t current_color = COLOR_WHITE;
static vga_pos_t cursor_position = { 0, 0 };

uint8_t vga_make_color(enum vga_color fg, enum vga_color bg)
{
    // fg = WHITE     = 0x0F = 0000 1111
    // bg = RED       = 0x04 = 0000 0100
    //
    // bg << 4        = 0100 0000 = 0x40
    // fg | (bg << 4) = 0100 1111 = 0x4F (white on red)
    return fg | (bg << 4);
}

uint16_t vga_entry(char c, uint8_t attr)
{
    // (char) 'A'  = 0x41         = 00000000 01000001
    // (attr) 0x4F = white on red = 01001111
    //
    // attr << 8       = 01001111 00000000
    // c | (attr << 8) = 01001111 01000001  = 0x4F41
    return (uint16_t)c | ((uint16_t)attr << 8);
}

void vga_set_color(enum vga_color fg, enum vga_color bg)
{
    current_color = vga_make_color(fg, bg);
}

uint8_t vga_get_color(void)
{
    return current_color;
}

void vga_set_cursor(vga_pos_t pos)
{
    cursor_position = pos;
}

vga_pos_t vga_get_cursor(void)
{
    return cursor_position;
}

void vga_putchar(const char c)
{
    vga_putchar_at(c, cursor_position);

    if (++cursor_position.x >= VGA_WIDTH) {
        cursor_position.x = 0;
        cursor_position.y++;
    }
}

void vga_putchar_at(const char c, vga_pos_t pos)
{
    uint16_t offset = (uint16_t)pos.y * VGA_WIDTH + pos.x;
    if (offset >= VGA_WIDTH * VGA_HEIGHT)
        return;

    VGA_BUFFER[offset] = vga_entry(c, current_color);
}

void vga_puts(const char* str)
{
    while(*str) {
        vga_putchar(*str);
        str++;
    }
}

void vga_clear(void)
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUFFER[i] = vga_entry(' ', vga_make_color(COLOR_BLACK, COLOR_BLACK));

    cursor_position.x = 0;
    cursor_position.y = 0;
}

void vga_init(void)
{
    vga_set_color(COLOR_WHITE, COLOR_BLACK);
    vga_clear();
}
