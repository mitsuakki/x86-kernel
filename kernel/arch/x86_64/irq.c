#include "irq.h"

#include "../../drivers/vga.h"
#include "../../lib/io.h"

static uint64_t tick_count = 0;

void irq_handler(registers_t *r)
{
    if (r->int_no == 32) {
        vga_puts("t:");
        vga_puthex(tick_count++);
        return;
    }

    if (r->int_no == 33) {
        uint8_t scancode = inb(0x60);
        vga_puts("k:");
        vga_puthex(scancode);
        return;
    }

    vga_puts("irq:");
    vga_puthex(r->int_no);
}
