#include "pic.h"
#include "../../lib/io.h"

// Helper: return the DATA port for a given IRQ line.
// IRQ 0-7 go to the master, IRQ 8-15 go to the slave.
static uint16_t irq_port(uint8_t line)
{
    if (line < 8)
        return MASTER_DATA;    // IRQ 0..7: master PIC
    return SLAVE_DATA;         // IRQ 8..15: slave PIC
}

// Helper: return the IMR bit number (0-7) for a given IRQ line.
// The master handles bits 0-7 directly. The slave handles bits 0-7 too,
// but its IRQ numbers start at 8, so we subtract 8.
static uint8_t irq_bit(uint8_t line)
{
    if (line < 8)
        return line;           // Master: IRQ line maps to the same bit
    return line - 8;           // Slave: IRQ 8 becomes bit 0, etc.
}

// Send End-Of-Interrupt signal to the PIC(s) after handling an IRQ.
// For IRQ 0-7 (master), send EOI to the master only.
// For IRQ 8-15 (slave), send EOI to both slave and master, because
// the slave is cascaded through the master's IRQ2 line and the
// master needs to clear its in-service bit for IRQ2 as well.
void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8)
        outb(SLAVE_COMMAND, PIC_EOI); // Acknowledge the slave first
    outb(MASTER_COMMAND, PIC_EOI);    // Then acknowledge the master
}

// Unmask all IRQs by clearing both IMRs to zero.
void pic_enable(void)
{
    outb(MASTER_DATA, 0x00);   // Master IMR = 0: nothing masked
    outb(SLAVE_DATA,  0x00);   // Slave  IMR = 0: nothing masked
}

// Mask all IRQs by setting both IMRs to 0xFF.
void pic_disable(void)
{
    outb(MASTER_DATA, 0xFF);   // M masked
    outb(SLAVE_DATA,  0xFF);   // S masked
}

// Remap both 8259A PICs so hardware IRQs land on vectors 32-47
// instead of the default 0x08-0x0F / 0x70-0x77. The default ranges
// overlap with CPU exceptions (0-31), which causes chaos.
//
// Initialization sequence:
//   1. Save current masks
//   2. ICW1 to COMMAND port (start init)
//   3. ICW2 to DATA port   (set base vector)
//   4. ICW3 to DATA port   (configure cascade)
//   5. ICW4 to DATA port   (set x86 mode)
//   6. Restore saved masks
//
// io_wait() gives the PIC time to process each command word.
// Not strictly needed on QEMU, but it costs nothing and keeps
// compatibility with real hardware.
void pic_remap(void)
{
    // Step 1: save the current interrupt mask registers.
    uint8_t saved_master = inb(MASTER_DATA);
    uint8_t saved_slave  = inb(SLAVE_DATA);

    // Step 2: ICW1. Start init, cascaded, edge-triggered, ICW4 follows.
    outb(MASTER_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(SLAVE_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    // Step 3: ICW2. Set the base interrupt vector for each PIC.
    // Master: IRQ0 fires INT32. Slave: IRQ8 fires INT40.
    outb(MASTER_DATA, PIC_OFFSET_MASTER);
    io_wait();

    outb(SLAVE_DATA, PIC_OFFSET_SLAVE);
    io_wait();

    // Step 4: ICW3. Tell the master "slave is on your IRQ2 line"
    // and tell the slave "your cascade identity is 2".
    outb(MASTER_DATA, ICW3_MASTER_CASCADE);
    io_wait();

    outb(SLAVE_DATA, ICW3_SLAVE_ID);
    io_wait();

    // Step 5: ICW4. x86 mode, normal EOI, non-buffered.
    outb(MASTER_DATA, ICW4_8086);
    io_wait();

    outb(SLAVE_DATA, ICW4_8086);
    io_wait();

    // Step 6: restore the saved masks.
    outb(MASTER_DATA, saved_master);
    outb(SLAVE_DATA,  saved_slave);
}

// Mask one IRQ line by setting its bit to 1 in the IMR.
// line must be between 0 and 15.
void irq_mask(uint8_t line)
{
    uint16_t port = irq_port(line);      // Which PIC controls this line?
    uint8_t  bit  = irq_bit(line);       // Which bit in its IMR?
    uint8_t  imr  = inb(port);           // Read the current mask
    outb(port, imr | (1 << bit));        // Set the bit to 1 (masked)
}

// Unmask one IRQ line by clearing its bit to 0 in the IMR.
// line must be between 0 and 15.
void irq_unmask(uint8_t line)
{
    uint16_t port = irq_port(line);      // Which PIC controls this line?
    uint8_t  bit  = irq_bit(line);       // Which bit in its IMR?
    uint8_t  imr  = inb(port);           // Read the current mask
    outb(port, imr & ~(1 << bit));       // Clear the bit to 0 (unmasked)
}
