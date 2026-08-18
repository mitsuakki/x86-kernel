#include "ps2.h"
#include "../lib/io.h"

// Wait until the output buffer has a byte to read (status bit 0 = 1).
// Must be called before reading from the data port (0x60).
// Returns 1 on success, 0 if the controller never signals ready.
int ps2_wait_output(void)
{
    uint32_t timeout = 100000;
    while (--timeout) {
        if (inb(PS2_STATUS_REGISTER) & PS2_STAT_OUT_FULL)
            return 1;
    }
    return 0;
}

// Wait until the input buffer is free (status bit 1 = 0).
// Must be called before writing to the data port (0x60) or command port (0x64).
// Returns 1 on success, 0 on timeout.
int ps2_wait_input(void)
{
    uint32_t timeout = 100000;
    while (--timeout) {
        if (!(inb(PS2_STATUS_REGISTER) & PS2_STAT_IN_FULL))
            return 1;
    }
    return 0;
}

// Send a command to the controller (written to 0x64).
void ps2_controller_command(uint8_t command)
{
    if (ps2_wait_input())
        outb(PS2_COMMAND_REGISTER, command);
}

// Send a command to the controller, followed by a data byte written to 0x60.
void ps2_controller_command_data(uint8_t command, uint8_t data)
{
    ps2_controller_command(command);
    ps2_write_data(data);
}

// Read one byte from the data port (0x60). Returns 0 on timeout.
uint8_t ps2_read_data(void)
{
    return ps2_wait_output() ? inb(PS2_DATA_PORT) : 0;
}

// Write one byte to the data port (0x60) - normally a command or data for the device.
void ps2_write_data(uint8_t byte)
{
    if (ps2_wait_input())
        outb(PS2_DATA_PORT, byte);
}

// Read the Configuration Byte from the controller (command 0x20, then read data).
uint8_t ps2_read_config(void)
{
    ps2_controller_command(PS2_CONTROLLER_READ_CONFIG);
    return ps2_read_data();
}

// Write a new Configuration Byte to the controller (command 0x60, then data byte).
void ps2_write_config(uint8_t config)
{
    ps2_controller_command(PS2_CONTROLLER_WRITE_CONFIG);
    ps2_write_data(config);
}

// Initialize the PS/2 controller, following the initialization sequence from
// wiki.osdev.org. Each step checks the expected answer and bails out early
// if the hardware doesn't behave.
void ps2_init(void)
{
    uint8_t config;
    uint8_t dual_channel;

    // Step 1: Initialise USB Controllers
    // Skipped: no USB support yet. (Only matters on machines with a USB
    // keyboard emulated as PS/2.)

    // Step 2: Determine if the PS/2 Controller Exists
    // Skipped: we assume the controller is present, since the BIOS sets it
    // up on every machine this kernel targets.

    // Step 3: Disable Devices
    // Shut both ports down so the devices stay quiet while we configure.
    ps2_controller_command(PS2_CONTROLLER_DISABLE_PORT1);
    ps2_controller_command(PS2_CONTROLLER_DISABLE_PORT2);

    // Step 4: Flush The Output Buffer
    // Drain any stale byte left over from the BIOS before sending commands.
    while (ps2_wait_output())
        inb(PS2_DATA_PORT);

    // Step 5: Set the Controller Configuration Byte
    // Disable IRQs and scancode translation so nothing fires during tests.
    config = ps2_read_config();
    config &= ~(PS2_CONFIG_PORT1_IRQ | PS2_CONFIG_PORT2_IRQ | PS2_CONFIG_PORT1_XLATE);
    ps2_write_config(config);

    // Step 6: Perform Controller Self Test
    // Expect 0x55 = pass. Anything else means the controller is broken.
    ps2_controller_command(PS2_CONTROLLER_TEST_SELF);
    if (ps2_read_data() != PS2_CONTROLLER_SELF_TEST_OK)
        return;

    // Step 7: Determine If There Are 2 Channels
    // The self test may reset the configuration byte, so read it again.
    // On single-channel controllers bit 5 is always set: if it reads clear,
    // a second channel exists.
    config = ps2_read_config();
    dual_channel = (config & PS2_CONFIG_PORT2_CLOCK_DIS) ? 0 : 1;

    // Step 8: Perform Interface Tests
    // 0xAB tests port 1, 0xA9 tests port 2. Each answers 0x00 on pass.
    ps2_controller_command(PS2_CONTROLLER_TEST_PORT1);
    if (ps2_read_data() != PS2_PORT_TEST_OK)
        return;

    if (dual_channel) {
        ps2_controller_command(PS2_CONTROLLER_TEST_PORT2);
        if (ps2_read_data() != PS2_PORT_TEST_OK)
            return;
    }

    // Step 9: Enable Devices
    // Turn IRQs and translation back on, unblock the clock lines, then
    // enable the ports themselves.
    config = ps2_read_config();
    config |= PS2_CONFIG_PORT1_IRQ | PS2_CONFIG_PORT1_XLATE;
    config &= ~(PS2_CONFIG_PORT1_CLOCK_DIS | PS2_CONFIG_PORT2_CLOCK_DIS);
    if (dual_channel)
        config |= PS2_CONFIG_PORT2_IRQ;
    ps2_write_config(config);

    ps2_controller_command(PS2_CONTROLLER_ENABLE_PORT1);
    if (dual_channel)
        ps2_controller_command(PS2_CONTROLLER_ENABLE_PORT2);

    // Step 10: Reset Devices
    // 0xFF makes the device reset and run its self test. It answers ACK
    // (0xFA) first, then 0xAA once the self test passes.
    ps2_write_data(PS2_DEVICE_CMD_RESET);
    if (ps2_read_data() != PS2_DEVICE_RESP_ACK)
        return;
    if (ps2_read_data() != PS2_DEVICE_RESP_SELF_TEST_OK)
        return;
}
