#ifndef KERNEL_INTERRUPTS_H
#define KERNEL_INTERRUPTS_H

#include "../../lib/stdbool.h"

static inline void enable_interrupts(void)
{
    // sti: Set Interrupt Flags
    __asm__ volatile ("sti");
}

static inline void disable_interrupts(void)
{
    // cli: Clear Interrupt Flags
    __asm__ volatile ("cli");
}

static inline bool are_interrupts_enabled(void)
{
    unsigned long flags;
    __asm__ volatile ("pushf; pop %0" : "=r"(flags));
    return (flags >> 9) & 1;
}

#endif // KERNEL_INTERRUPTS_H
