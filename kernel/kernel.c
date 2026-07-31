#include "drivers/vga_text.h"
#include "arch/x86_64/idt.h"

void kernel_main()
{
    idt_init();
    
    clear_screen();
    write("I love my girlfriend!", COLOR_WHITE);

    for (;;) {}
}
