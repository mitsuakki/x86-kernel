#include "drivers/vga.h"
#include "arch/x86_64/idt.h"

void kernel_main()
{
    idt_init();
    vga_init();

    __asm__ volatile ("int $0x3"); // #BP Breakpoint

    for (;;) {} // kernel never stop
}
