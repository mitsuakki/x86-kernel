__attribute__((noreturn))
void exception_handler(void) // match the ASM declaration in ../idt.asm
{
    __asm__ volatile ("cli; hlt"); // hangs the computer
}
