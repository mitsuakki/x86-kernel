#ifndef PS2_H
#define PS2_H

#include "../lib/stdint.h"

// PS/2 controller I/O ports.
// 0x60 carries data (both directions), 0x64 is the status register when read
// and the command register when written.
#define PS2_DATA_PORT         0x60 // Data port (read/write): exchange bytes with the device
#define PS2_STATUS_REGISTER   0x64 // Status register (read): controller state
#define PS2_COMMAND_REGISTER  0x64 // Command register (write): send a command to the controller

// Status register bits (read from 0x64). Poll before touching the data port.
//   [0] OUT_FULL      - output buffer has a byte waiting to be read from 0x60
//   [1] IN_FULL       - input buffer is busy; don't write to 0x60 or 0x64 yet
//   [2] SYS_FLAG      - set by firmware once POST passes, cleared on reset
//   [3] CMD_DATA      - 0 = input buffer holds data for the device
//                       1 = input buffer holds a controller command
//   [4] UNKNOWN       - chipset specific (maybe keyboard lock)
//   [5] UNKNOWN       - chipset specific (maybe receive timeout or second port output buffer full)
//   [6] TIMEOUT_ERROR - time-out error: 0 = none, 1 = timeout
//   [7] PARITY_ERROR  - parity error: 0 = none, 1 = parity error
//
#define PS2_STAT_OUT_FULL      (1 << 0) // Bit 0: output buffer full (readable byte at 0x60)
#define PS2_STAT_IN_FULL       (1 << 1) // Bit 1: input buffer full (0x60/0x64 busy - don't write)
#define PS2_STAT_SYS_FLAG      (1 << 2) // Bit 2: POST passed (set by firmware, cleared on reset)
#define PS2_STAT_CMD_DATA      (1 << 3) // Bit 3: 0 = input buffer holds device data, 1 = holds controller command
#define PS2_STAT_UNKOWN1       (1 << 4) // Bit 4: chipset specific (maybe keyboard lock)
#define PS2_STAT_UNKOWN2       (1 << 5) // Bit 5: chipset specific (maybe receive timeout or second port output buffer full)
#define PS2_STAT_TIMEOUT_ERROR (1 << 6) // Bit 6: time-out error (0 = none, 1 = timeout)
#define PS2_STAT_PARITY_ERROR  (1 << 7) // Bit 7: parity error (0 = none, 1 = parity error)

// Controller commands (written to 0x64). Some take an extra data byte
// written to 0x60.
#define PS2_CONTROLLER_READ_CONFIG    0x20 // Read Configuration Byte ("byte 0" of internal RAM)
#define PS2_CONTROLLER_READ_RAM_BASE  0x21 // Read "byte N" of internal RAM: N = command & 0x1F (range 0x21-0x3F)
#define PS2_CONTROLLER_WRITE_CONFIG   0x60 // Write next byte to Configuration Byte ("byte 0")
#define PS2_CONTROLLER_WRITE_RAM_BASE 0x61 // Write next byte to "byte N" of internal RAM: N = command & 0x1F (range 0x61-0x7F)
#define PS2_CONTROLLER_DISABLE_PORT2  0xA7 // Disable second port (only if 2 ports supported)
#define PS2_CONTROLLER_ENABLE_PORT2   0xA8 // Enable second port (only if 2 ports supported)
#define PS2_CONTROLLER_TEST_PORT2     0xA9 // Test second port (only if 2 ports supported)
#define PS2_CONTROLLER_TEST_SELF      0xAA // Test controller
#define PS2_CONTROLLER_TEST_PORT1     0xAB // Test first port
#define PS2_CONTROLLER_DIAG_DUMP      0xAC // Diagnostic dump: read all bytes of internal RAM
#define PS2_CONTROLLER_DISABLE_PORT1  0xAD // Disable first port
#define PS2_CONTROLLER_ENABLE_PORT1   0xAE // Enable first port
#define PS2_CONTROLLER_READ_INPUT     0xC0 // Read controller input port
#define PS2_CONTROLLER_COPY_IN_LO     0xC1 // Copy input port bits 0-3 into status bits 4-7
#define PS2_CONTROLLER_COPY_IN_HI     0xC2 // Copy input port bits 4-7 into status bits 4-7
#define PS2_CONTROLLER_READ_OUT       0xD0 // Read Controller Output Port
#define PS2_CONTROLLER_WRITE_OUT      0xD1 // Write next byte to Controller Output Port (check output buffer empty first)
#define PS2_CONTROLLER_WRITE_OBUF1    0xD2 // Write next byte to first port's output buffer: fakes a byte received from port 1 (only if 2 ports)
#define PS2_CONTROLLER_WRITE_OBUF2    0xD3 // Write next byte to second port's output buffer: fakes a byte received from port 2 (only if 2 ports)
#define PS2_CONTROLLER_WRITE_PORT2    0xD4 // Write next byte to second port's input buffer: sends it to the device on port 2 (only if 2 ports)
#define PS2_CONTROLLER_PULSE_BASE     0xF0 // Pulse output line low for 6 ms (range 0xF0-0xFF)
// Bits 0-3 pick which lines to pulse: 0 = pulse, 1 = don't pulse. Bit 0 is the reset line.

// Configuration Byte (read via command 0x20, write via command 0x60 + data byte).
#define PS2_CONFIG_PORT1_IRQ       (1 << 0) // Bit 0: port 1 interrupt enabled (1 = enabled)
#define PS2_CONFIG_PORT2_IRQ       (1 << 1) // Bit 1: port 2 interrupt enabled (only if 2 ports)
#define PS2_CONFIG_SYS_FLAG        (1 << 2) // Bit 2: 1 = POST passed, 0 = your OS shouldn't be running
#define PS2_CONFIG_ZERO3           (1 << 3) // Bit 3: must be zero
#define PS2_CONFIG_PORT1_CLOCK_DIS (1 << 4) // Bit 4: port 1 clock disabled (1 = disabled)
#define PS2_CONFIG_PORT2_CLOCK_DIS (1 << 5) // Bit 5: port 2 clock disabled (only if 2 ports)
#define PS2_CONFIG_PORT1_XLATE     (1 << 6) // Bit 6: port 1 translation enabled (scancode set 2 to set 1)
#define PS2_CONFIG_ZERO7           (1 << 7) // Bit 7: must be zero

// Controller Output Port (read via command 0xD0, write via 0xD1 + data byte).
#define PS2_OUT_SYS_RESET       (1 << 0) // Bit 0: ALWAYS 1 - setting to 0 locks up the computer. Use command 0xFE to pulse reset instead.
#define PS2_OUT_A20_GATE        (1 << 1) // Bit 1: A20 gate
#define PS2_OUT_PORT2_CLOCK     (1 << 2) // Bit 2: second port clock (only if 2 ports)
#define PS2_OUT_PORT2_DATA      (1 << 3) // Bit 3: second port data (only if 2 ports)
#define PS2_OUT_OBUF1_FULL      (1 << 4) // Bit 4: output buffer full with byte from first port (IRQ1)
#define PS2_OUT_OBUF2_FULL      (1 << 5) // Bit 5: output buffer full with byte from second port (IRQ12, only if 2 ports)
#define PS2_OUT_PORT1_CLOCK     (1 << 6) // Bit 6: first port clock
#define PS2_OUT_PORT1_DATA      (1 << 7) // Bit 7: first port data

// Controller self-test responses (answer to command 0xAA, read from 0x60).
#define PS2_CONTROLLER_SELF_TEST_OK   0x55 // Self-test passed
#define PS2_CONTROLLER_SELF_TEST_FAIL 0xFC // Self-test failed

// Port test responses (answer to commands 0xAB/0xA9).
#define PS2_PORT_TEST_OK        0x00 // Test passed
#define PS2_PORT_CLOCK_LOW      0x01 // Clock line stuck low
#define PS2_PORT_CLOCK_HIGH     0x02 // Clock line stuck high
#define PS2_PORT_DATA_LOW       0x03 // Data line stuck low
#define PS2_PORT_DATA_HIGH      0x04 // Data line stuck high

// Device commands (sent to the device by writing to 0x60).
// Every PS/2 device must support Identify and Disable Scanning.
#define PS2_DEVICE_CMD_IDENTIFY    0xF2 // Identify device
#define PS2_DEVICE_CMD_ENABLE      0xF4 // Enable scanning (device will send data)
#define PS2_DEVICE_CMD_DISABLE     0xF5 // Disable scanning (device ignores user input)
#define PS2_DEVICE_CMD_RESET       0xFF // Reset and start self-test

// Device responses (read from 0x60).
#define PS2_DEVICE_RESP_ACK              0xFA // Command acknowledged
#define PS2_DEVICE_RESP_RESEND           0xFE // Resend last byte
#define PS2_DEVICE_RESP_SELF_TEST_OK     0xAA // Self-test passed (after reset or power up)
#define PS2_DEVICE_RESP_SELF_TEST_FAIL   0xFC // Self-test failed
#define PS2_DEVICE_RESP_SELF_TEST_FAIL2  0xFD // Self-test failed (alternate)

// Device type IDs (response to the Identify command).
// Protocol: Disable → Identify → wait ACK → read up to 2 ID bytes → Enable.
// No response (timeout) means an ancient AT keyboard.
//
// 1-byte IDs:
#define PS2_DEVICE_ID_STD_MOUSE     0x00 // Standard PS/2 mouse
#define PS2_DEVICE_ID_SCROLL_MOUSE  0x03 // Mouse with scroll wheel
#define PS2_DEVICE_ID_5BTN_MOUSE    0x04 // 5-button mouse

// 2-byte IDs: first byte 0xAB, second byte identifies the keyboard.
// With translation enabled, the second byte differs (noted below).
#define PS2_DEVICE_ID_MF2_KB_B0     0xAB // First byte common to most keyboards
#define PS2_DEVICE_ID_MF2_KB_B1     0x83 // MF2 keyboard: (0xAB, 0x83) - translated (0xAB, 0x41)
#define PS2_DEVICE_ID_MF2_KB_ALT_B1 0xC1 // MF2 keyboard alternate: (0xAB, 0xC1) - translated (0xAB, 0xC1)
#define PS2_DEVICE_ID_SHORT_KB_B1   0x84 // "Short" keyboards (IBM ThinkPads, Spacesaver): (0xAB, 0x84) - translated (0xAB, 0x54)
#define PS2_DEVICE_ID_NCD_N97_B1    0x85 // NCD N-97 / 122-key Host Connect keyboard: (0xAB, 0x85)
#define PS2_DEVICE_ID_122KEY_B1     0x86 // 122-key keyboard: (0xAB, 0x86)
#define PS2_DEVICE_ID_JP_G_B1       0x90 // Japanese "G" keyboard: (0xAB, 0x90)
#define PS2_DEVICE_ID_JP_P_B1       0x91 // Japanese "P" keyboard: (0xAB, 0x91)
#define PS2_DEVICE_ID_JP_A_B1       0x92 // Japanese "A" keyboard: (0xAB, 0x92)
#define PS2_DEVICE_ID_NCD_SUN_B0    0xAC // NCD Sun layout keyboard: first byte
#define PS2_DEVICE_ID_NCD_SUN_B1    0xA1 // NCD Sun layout keyboard: second byte (0xAC, 0xA1)

int ps2_wait_output(void);
int ps2_wait_input(void);

void ps2_controller_command(uint8_t command);
void ps2_controller_command_data(uint8_t command, uint8_t data);

uint8_t ps2_read_data(void);
void ps2_write_data(uint8_t byte);

uint8_t ps2_read_config(void);
void ps2_write_config(uint8_t config);

void ps2_init(void);

#endif // PS2_H
