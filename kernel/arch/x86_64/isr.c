#include "isr.h"
#include "idt.h"
#include "../../drivers/vga.h"

static const char *const exception_names[32] = {
    "#DE Division Error",
    "#DB Debug",
    "Non-maskable Interrupt",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR Bound Range Exceeded",
    "#UD Invalid Opcode",
    "#NM Device Not Available",
    "#DF Double Fault",
    "Coprocessor Segment Overrun",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack-Segment Fault",
    "#GP General Protection Fault",
    "#PF Page Fault",
    "Reserved",
    "#MF x87 Floating-Point Exception",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XM SIMD Floating-Point Exception",
    "#VE Virtualization Exception",
    "#CP Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "#HV Hypervisor Injection Exception",
    "#VC VMM Communication Exception",
    "#SX Security Exception",
    "Reserved",
};

#define DUMP(reg) do { \
    vga_puts(#reg "="); vga_puthex(r->reg); \
} while(0)

__attribute__((noreturn))
void isr_handler(registers_t *r)
{
    vga_puts(exception_names[r->int_no]);
    vga_putchar('\n');

    DUMP(rax); DUMP(rbx);
    DUMP(rcx); DUMP(rdx);
    DUMP(rsi); DUMP(rdi);
    DUMP(rbp); DUMP(r8);
    DUMP(r9);  DUMP(r10);
    DUMP(r11); DUMP(r12);
    DUMP(r13); DUMP(r14);
    DUMP(r15); DUMP(rip);
    DUMP(rflags); DUMP(cs);
    DUMP(rsp); DUMP(ss);
    DUMP(int_no); DUMP(err_code);

    if (r->int_no == EXCEPTION_PAGE_FAULT) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        vga_puts("cr2="); vga_puthex(cr2);
    }

    __asm__ volatile ("cli; hlt");
    __builtin_unreachable();
}
