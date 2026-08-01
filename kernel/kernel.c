#include "drivers/vga_text.h"
#include "arch/x86_64/idt.h"

void kernel_main()
{
    idt_init();
    
    clear_screen();
    write("I love my girlfriend!", COLOR_WHITE);

    // M2 test: trigger breakpoint exception (vector 3).
    // Should print "EXCEPTION 0x03 HALTED" then halt.
    __asm__ volatile ("int $0x3");

    for (;;) {}
}
