#include "irq.h"

#include "../../drivers/keyboard.h"
#include "../../drivers/vga.h"

static uint64_t tick_count = 0;

void irq_handler(registers_t *r)
{
    if (r->int_no == 32) {
        // vga_puts("t:");
        // vga_puthex(tick_count++);
        return;
    }

    if (r->int_no == 33) {
        uint8_t scancode = keyboard_get_scancode();
        char c = keyboard_scancode_to_ascii(scancode);
        if (c)
            vga_putchar(c);
        return;
    }

    vga_puts("irq:");
    vga_puthex(r->int_no);
}
