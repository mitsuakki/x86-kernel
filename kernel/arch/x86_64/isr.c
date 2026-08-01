__attribute__((noreturn))
void exception_handler(void) // called by isr_common_stub in isr_stubs.asm
{
    __asm__ volatile ("cli; hlt"); // hangs the computer
    __builtin_unreachable();
}
