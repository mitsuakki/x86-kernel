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
    if (c == '\n') {
        cursor_position.x = 0;
        if (cursor_position.y < VGA_HEIGHT - 1)
            cursor_position.y++;
        return;
    }

    vga_putchar_at(c, cursor_position);

    if (++cursor_position.x >= VGA_WIDTH) {
        cursor_position.x = 0;
        if (cursor_position.y < VGA_HEIGHT - 1)
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

void vga_puthex(uint64_t value)
{
    vga_putchar('0');
    vga_putchar('x');

    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        vga_putchar("0123456789ABCDEF"[nibble]);
    }

    vga_putchar('\n');
}

void vga_clear(void)
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUFFER[i] = vga_entry(' ', current_color);
    cursor_position = (vga_pos_t){0, 0};
}

void vga_init(void)
{
    vga_set_color(COLOR_WHITE, COLOR_BLACK);
    vga_clear();
}

void vga_draw_heart(uint8_t start_x, uint8_t start_y)
{
    for (int row = 0; row < HEART_HEIGHT; row++) {
        for (int col = 0; col < HEART_WIDTH; col++) {
            // Normalize col/row to fixed-point range roughly [-1.5, 1.5]
            // Text cells are taller than wide, so col is scaled down
            // (~2x) to compensate and keep the heart visually round.
            long x = ((col - HEART_WIDTH / 2) * HEART_SCALE) / (HEART_WIDTH / 3);
            long y = ((HEART_HEIGHT / 2 - row) * HEART_SCALE) / (HEART_HEIGHT / 3);

            // (x^2 + y^2 - scale^2)^3 <= x^2 * y^3
            long x2 = x * x / HEART_SCALE;
            long y2 = y * y / HEART_SCALE;
            long lhs_base = x2 + y2 - HEART_SCALE;
            long lhs = lhs_base * lhs_base / HEART_SCALE * lhs_base / HEART_SCALE;
            long rhs = x2 * y * y2 / HEART_SCALE / HEART_SCALE;

            if (lhs <= rhs) {
                vga_putchar_at('*', (vga_pos_t){ start_x + col, start_y + row });
            }
        }
    }
}
