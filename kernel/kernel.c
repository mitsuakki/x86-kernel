#include "arch/x86_64/pic.h"
#include "arch/x86_64/idt.h"

#include "drivers/vga.h"

void kernel_main()
{
    pic_remap();
    idt_init();

    vga_init();

    __asm__ volatile ("int $0x3"); // #BP Breakpoint
    for (;;) {} // kernel never stop
}
