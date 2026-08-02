#ifndef KERNEL_IO_H
#define KERNEL_IO_H

#include "stdint.h"

#define KERNEL_DEFAULT_IO_PORT 0x80

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %b0, %w1" :: "a"(value), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %w1, %b0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

static inline void io_wait(void)
{
    outb(KERNEL_DEFAULT_IO_PORT, 0);
}

#endif // KERNEL_IO_H
