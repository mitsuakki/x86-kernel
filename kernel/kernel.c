#include "drivers/vga.h"
#include "arch/x86_64/idt.h"

#define HEART_WIDTH  40
#define HEART_HEIGHT 20
#define HEART_SCALE  1000  // fixed-point precision

void kernel_main()
{
    idt_init();
    vga_init();

    uint8_t start_x = (VGA_WIDTH - HEART_WIDTH) / 2;
    uint8_t start_y = (VGA_HEIGHT - HEART_HEIGHT) / 2;
    uint8_t saved_color = vga_get_color();

    vga_set_color(COLOR_LIGHT_RED, COLOR_BLACK);

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
                vga_pos_t pos = { start_x + col, start_y + row };
                vga_putchar_at('*', pos);
            }
        }
    }

    vga_set_color(saved_color, COLOR_BLACK);

    for (;;) {} // kernel never stop
}
