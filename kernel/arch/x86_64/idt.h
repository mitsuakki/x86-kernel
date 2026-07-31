#ifndef IDT_H
#define IDT_H

#include "../../lib/stdint.h"

// In order to make interrupts, we need an IDT.
// When an interrupt is fired, the CPU uses the vector as an index into the IDT.
// The CPU reads the entry of the IDT in order to figure out what to do prior to calling the ISR.
// and what the address of the handler is.
typedef struct {
    uint16_t isr_low;    // Lower 16 bits of ISR address
    uint16_t kernel_cs;  // Code segment selector (GDT kernel code, typically 0x08)
    uint8_t  ist;        // Interrupt Stack Table index (0-7), or 0 to use modified legacy stack
    uint8_t  attributes; // Gate type (bits 40-43) + attributes (bits 44-47)
                         // 0x8E = present, DPL 0, 64-bit interrupt gate
                         // 0x8F = present, DPL 0, 64-bit trap gate
                         // 0xEE = present, DPL 3, 64-bit interrupt gate (user-callable)
    uint16_t isr_mid;    // Middle 16 bits of ISR address
    uint32_t isr_high;   // Upper 32 bits of ISR address
    uint32_t reserved;   // Reserved, must be 0
} __attribute__((packed)) idt_entry_t;

// Interrupt gate vs trap gate:
// - Interrupt gate (type_attr & 0x1 == 0): CPU clears IF flag on entry. Use for hardware interrupts (IRQs).
// - Trap gate (type_attr & 0x1 == 1): CPU does NOT clear IF. Use for exceptions (page fault, breakpoint, etc.) and syscalls.
// Combine with the upper nibble (bits 44-47) which holds 0x8 (present + DPL, with storage bit set — 0x80):
#define IDT_GATE_INTERRUPT 0x8E
#define IDT_GATE_TRAP      0x8F

// Simply, an IDT is just a 256-entry array of descriptors
#define IDT_MAX_DESCRIPTORS 256

__attribute__((aligned(0x10)))
static idt_entry_t idt[IDT_MAX_DESCRIPTORS];

// We also need a special IDTR structure (10 bytes, packed)
// Packed struct is important here because the compiler may add padding between fields
// breaking hardware layout.
typedef struct {
    uint16_t limit; // 2 bytes so offset (O) & size (2)
    uint64_t base;  // 8 bytes so offset (2) & size (8)
} __attribute__((packed)) idtr_t;

// And of course, we have to define it.
static idtr_t idtr;

static inline void idt_init()
{
    idtr.base  = (uintptr_t)&idt[0];
    idtr.limit = (uint16_t)sizeof(idt_entry_t) * IDT_MAX_DESCRIPTORS - 1;

    // lidt loads the IDTR from memory. "m"(idtr) passes the struct directly
    // The CPU reads 10 bytes (2 limit + 8 base) and loads the IDT.
    __asm__ volatile ("lidt %0" :: "m"(idtr));
}

#endif // IDT_H
