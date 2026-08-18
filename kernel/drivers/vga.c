#include "vga.h"

// Text mode VGA driver, 80x25 characters at 0xB8000.
// Each screen cell is one 16-bit word: low byte = character,
// high byte = attribute (foreground color in bits 0-3, background in bits 4-7).

static uint8_t current_color = VGA_COLOR_WHITE;
static vga_pos_t cursor_position = { 0, 0 };

// Combine foreground and background color into one attribute byte.
uint8_t vga_make_color(enum vga_color fg, enum vga_color bg)
{
    // fg = WHITE     = 0x0F = 0000 1111
    // bg = RED       = 0x04 = 0000 0100
    //
    // bg << 4        = 0100 0000 = 0x40
    // fg | (bg << 4) = 0100 1111 = 0x4F (white on red)
    return fg | (bg << 4);
}

// Build one 16-bit VGA cell: attribute in the high byte, character in the low byte.
// The (uint8_t) cast matters: characters above 0x7F (CP437 accented
// letters) would otherwise sign-extend and corrupt the attribute byte.
uint16_t vga_entry(char c, uint8_t attr)
{
    // (char) 'A'  = 0x41         = 00000000 01000001
    // (attr) 0x4F = white on red = 01001111
    //
    // attr << 8       = 01001111 00000000
    // c | (attr << 8) = 01001111 01000001  = 0x4F41
    return (uint16_t)(uint8_t)c | ((uint16_t)attr << 8);
}

// Set the color used by all subsequent vga_put* calls.
void vga_set_color(enum vga_color fg, enum vga_color bg)
{
    current_color = vga_make_color(fg, bg);
}

uint8_t vga_get_color(void)
{
    return current_color;
}

// Move the (software) cursor. The hardware cursor is not moved yet:
// the screen is redrawn only through vga_put*.
void vga_set_cursor(vga_pos_t pos)
{
    cursor_position = pos;
}

vga_pos_t vga_get_cursor(void)
{
    return cursor_position;
}

// Print one character at the cursor and advance it.
// '\n' resets the column and moves one line down.
// No scrolling yet: output stops at the last line.
void vga_putchar(const char c)
{
    if (c == '\n') {
        cursor_position.x = 0;
        if (cursor_position.y < VGA_HEIGHT - 1)
            cursor_position.y++;
        return;
    }

    vga_putchar_at(c, cursor_position);

    // Wrap around at the end of the line.
    if (++cursor_position.x >= VGA_WIDTH) {
        cursor_position.x = 0;
        if (cursor_position.y < VGA_HEIGHT - 1)
            cursor_position.y++;
    }
}

// Write one character at an arbitrary position, without moving the cursor.
// Out-of-bounds positions are ignored.
void vga_putchar_at(const char c, vga_pos_t pos)
{
    uint16_t offset = (uint16_t)pos.y * VGA_WIDTH + pos.x;
    if (offset >= VGA_WIDTH * VGA_HEIGHT)
        return;

    VGA_BUFFER[offset] = vga_entry(c, current_color);
}

// Print a NUL-terminated string.
void vga_puts(const char* str)
{
    while(*str) {
        vga_putchar(*str);
        str++;
    }
}

// Print a 64-bit value as "0x" followed by 16 hex digits (always full width).
void vga_puthex(uint64_t value)
{
    vga_putchar('0');
    vga_putchar('x');

    // Extract one nibble at a time, most significant first.
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        vga_putchar("0123456789ABCDEF"[nibble]);
    }

    vga_putchar('\n');
}

// Fill the whole screen with spaces in the current color and reset the cursor.
void vga_clear(void)
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUFFER[i] = vga_entry(' ', current_color);
    cursor_position = (vga_pos_t){0, 0};
}

// Set the default kernel color scheme (white on black) and clear the screen.
void vga_init(void)
{
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_clear();
}

// Draw an ASCII heart centered at (start_x, start_y), width and height
// defined in vga.h.
void vga_draw_heart(uint8_t start_x, uint8_t start_y)
{
    // Heart equation in fixed point: (x^2 + y^2 - 1)^3 <= x^2 * y^3.
    // True = inside the heart: draw a '*'.
    for (int row = 0; row < VGA_KERNEL_HEART_HEIGHT; row++) {
        for (int col = 0; col < VGA_KERNEL_HEART_WIDTH; col++) {
            // Normalize col/row to fixed-point range roughly [-1.5, 1.5]
            // Text cells are taller than wide, so col is scaled down
            // (~2x) to compensate and keep the heart visually round.
            long x = ((col - VGA_KERNEL_HEART_WIDTH / 2) * VGA_KERNEL_HEART_SCALE) / (VGA_KERNEL_HEART_WIDTH / 3);
            long y = ((VGA_KERNEL_HEART_HEIGHT / 2 - row) * VGA_KERNEL_HEART_SCALE) / (VGA_KERNEL_HEART_HEIGHT / 3);

            // (x^2 + y^2 - scale^2)^3 <= x^2 * y^3
            long x2 = x * x / VGA_KERNEL_HEART_SCALE;
            long y2 = y * y / VGA_KERNEL_HEART_SCALE;
            long lhs_base = x2 + y2 - VGA_KERNEL_HEART_SCALE;
            long lhs = lhs_base * lhs_base / VGA_KERNEL_HEART_SCALE * lhs_base / VGA_KERNEL_HEART_SCALE;
            long rhs = x2 * y * y2 / VGA_KERNEL_HEART_SCALE / VGA_KERNEL_HEART_SCALE;

            if (lhs <= rhs) {
                vga_putchar_at('*', (vga_pos_t){ start_x + col, start_y + row });
            }
        }
    }
}
