#ifndef KERNEL_PIC_H
#define KERNEL_PIC_H

#include "../../lib/stdint.h"

// The 8259A PIC is a programmable interrupt controller that gathers signals from up to 15 hardware devices.
// It prioritizes them, and tells the CPU which interrupt vector to use so each device gets its own handler.
// Without it, every device would share the same CPU pin with no way to tell them apart.

// Each 8259A PIC has two I/O ports:
//   COMMAND:  write ICW1/OCW2/OCW3 here, read IRR/ISR
//   DATA:     write ICW2/ICW3/ICW4/OCW1 (IMR) here, read IMR
//
#define MASTER_COMMAND  0x20   // Master PIC command port
#define MASTER_DATA     0x21   // Master PIC data port
#define SLAVE_COMMAND   0xA0   // Slave PIC command port
#define SLAVE_DATA      0xA1   // Slave PIC data port

// ICW1 is written to the COMMAND port. It starts initialization.
// Bits:
//   [4] INIT      - must be 1 to begin init sequence
//   [3] LEVEL     - 1 = level-triggered, 0 = edge-triggered
//   [2] INTERVAL4 - 1 = interval 4, 0 = interval 8 (MCS-80 only)
//   [1] SINGLE    - 1 = single PIC, 0 = cascaded
//   [0] ICW4      - 1 = ICW4 will follow (required for x86)
//
#define ICW1_ICW4       0x01  // ICW4 will be sent next
#define ICW1_SINGLE     0x02  // Single PIC mode (no slave)
#define ICW1_INTERVAL4  0x04  // Address interval 4
#define ICW1_LEVEL      0x08  // Level-triggered mode
#define ICW1_INIT       0x10  // Begin initialization sequence

// ICW2 is written to the DATA port. It sets the base interrupt vector
// for the PIC. IRQn triggers interrupt base + n on the CPU.
// The base must be aligned to 8 vectors (low 3 bits = 0).
//
#define PIC_OFFSET_MASTER  0x20  // Master: IRQ0..IRQ7 map to INT32..INT39
#define PIC_OFFSET_SLAVE   0x28  // Slave:  IRQ8..IRQ15 map to INT40..INT47

// ICW3 is written to the DATA port. It configures the cascade wiring
// between master and slave.
//   Master: a bitmap. Bit = 1 means "a slave is on this IRQ line".
//   Slave:  a 3-bit ID (0-7) telling which master IRQ line it is wired to.
//
#define CASCADE_IRQ            2                 // Slave is connected to master IRQ2
#define ICW3_MASTER_CASCADE   (1 << CASCADE_IRQ) // Master: slave present on IRQ2
#define ICW3_SLAVE_ID          CASCADE_IRQ       // Slave:  my cascade ID is 2

// ICW4 is written to the DATA port. It sets the operating mode.
// Bits:
//   [0] 8086 - 1 = x86 mode, 0 = MCS-80/85 mode
//   [1] AUTO - 1 = auto EOI, 0 = normal EOI (we want normal)
//   [3] BUF  - buffered mode (bits 3:2 together)
//   [4] SFNM - special fully nested mode
//
#define ICW4_8086       0x01  // x86 (8086/88) mode
#define ICW4_AUTO       0x02  // Auto EOI (we don't use this)
#define ICW4_BUF_SLAVE  0x08  // Buffered mode, slave
#define ICW4_BUF_MASTER 0x0C  // Buffered mode, master
#define ICW4_SFNM       0x10  // Special fully nested mode

// OCW2 is written to the COMMAND port to send an EOI signal.
// Bits [7:5] = 001 means non-specific EOI (the common case).
//
#define PIC_EOI         0x20  // Non-specific End-Of-Interrupt

void pic_send_eoi(uint8_t irq);

void pic_enable(void);
void pic_disable(void);

void pic_remap(void);

void irq_mask(uint8_t line);
void irq_unmask(uint8_t line);

#endif // KERNEL_PIC_H
