#include "arch/x86_64/pic.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/interrupts.h"

#include "drivers/vga.h"
#include "drivers/keyboard.h"

void kernel_main()
{
    pic_remap();
    idt_init();

    vga_init();
    enable_interrupts();

    for (;;) {} // kernel never stop
}
