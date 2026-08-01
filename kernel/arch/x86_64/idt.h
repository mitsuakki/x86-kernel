#ifndef IDT_H
#define IDT_H

#include "../../lib/stdint.h"

#define GDT_OFFSET_KERNEL_CODE 0x18

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

// We also need a special IDTR structure (10 bytes, packed)
// Packed struct is important here because the compiler may add padding between fields
// breaking hardware layout.
typedef struct {
    uint16_t limit; // 2 bytes so offset (O) & size (2)
    uint64_t base;  // 8 bytes so offset (2) & size (8)
} __attribute__((packed)) idtr_t;


// Exception vectors — https://wiki.osdev.org/Exceptions
// "Err" column = CPU pushes error code onto stack.
#define EXCEPTION_DIVISION_BY_ZERO           0x00  // #DE  Fault      Err: no
#define EXCEPTION_DEBUG                      0x01  // #DB  Fault/Trap Err: no
#define EXCEPTION_NON_MASKABLE_INTERRUPT     0x02  // -    Interrupt  Err: no
#define EXCEPTION_BREAKPOINT                 0x03  // #BP  Trap       Err: no
#define EXCEPTION_OVERFLOW                   0x04  // #OF  Trap       Err: no
#define EXCEPTION_BOUND_RANGE_EXCEEDED       0x05  // #BR  Fault      Err: no
#define EXCEPTION_INVALID_OPCODE             0x06  // #UD  Fault      Err: no
#define EXCEPTION_DEVICE_NOT_AVAILABLE       0x07  // #NM  Fault      Err: no
#define EXCEPTION_DOUBLE_FAULT               0x08  // #DF  Abort      Err: yes (zero)
#define EXCEPTION_COPROCESSOR_SEGMENT_OVERRUN 0x09 // -    Fault      Err: no
#define EXCEPTION_INVALID_TSS                0x0A  // #TS  Fault      Err: yes
#define EXCEPTION_SEGMENT_NOT_PRESENT        0x0B  // #NP  Fault      Err: yes
#define EXCEPTION_STACK_SEGMENT_FAULT        0x0C  // #SS  Fault      Err: yes
#define EXCEPTION_GENERAL_PROTECTION_FAULT   0x0D  // #GP  Fault      Err: yes
#define EXCEPTION_PAGE_FAULT                 0x0E  // #PF  Fault      Err: yes
// Vector 0x0F — Reserved
#define EXCEPTION_X87_FLOATING_POINT         0x10  // #MF  Fault      Err: no
#define EXCEPTION_ALIGNMENT_CHECK            0x11  // #AC  Fault      Err: yes (zero)
#define EXCEPTION_MACHINE_CHECK              0x12  // #MC  Abort      Err: no
#define EXCEPTION_SIMD_FLOATING_POINT        0x13  // #XM  Fault      Err: no
#define EXCEPTION_VIRTUALIZATION             0x14  // #VE  Fault      Err: no
#define EXCEPTION_CONTROL_PROTECTION         0x15  // #CP  Fault      Err: yes
// Vectors 0x16–0x1B — Reserved
#define EXCEPTION_HYPERVISOR_INJECTION       0x1C  // #HV  Fault      Err: no
#define EXCEPTION_VMM_COMMUNICATION          0x1D  // #VC  Fault      Err: yes
#define EXCEPTION_SECURITY                   0x1E  // #SX  Fault      Err: yes
// Vector 0x1F — Reserved

void idt_init(void);
void idt_set_descriptor(uint8_t vector, void* isr, uint8_t flags);

#endif // IDT_H
