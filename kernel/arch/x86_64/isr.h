#ifndef KERNEL_ISR_H
#define KERNEL_ISR_H

#include "../../lib/stdint.h"

// Register frame saved by isr_common_stub in isr_stubs.asm.
//
// Stack layout after pushaq (growing upward -> higher addresses):
//
//  [rflags]      <- CPU IRET frame
//  [cs]
//  [rip]
//  [err_code]    <- CPU (or dummy 0 pushed by stub)
//  [int_no]      <- pushed by stub
//  [rax]         <- 1st push in isr_common_stub
//  [rbx]
//  [rcx]         (System V arg4)
//  [rdx]         (System V arg3)
//  [rsi]         (System V arg2)
//  [rdi]         (System V arg1)
//  [rbp]
//  [r8]          (System V arg5)
//  [r9]          (System V arg6)
//  [r10]
//  [r11]
//  [r12]
//  [r13]
//  [r14]
//  [r15]         <- last push, lowest address, RSP (stack pointer) points here
//
// Struct fields MUST match: first field = last push (lowest addr).

typedef struct {
    // pushed last -> lowest address → first fields
    uint64_t r15;        // [rsp + 0x00]
    uint64_t r14;        // [rsp + 0x08]
    uint64_t r13;        // [rsp + 0x10]
    uint64_t r12;        // [rsp + 0x18]
    uint64_t r11;        // [rsp + 0x20]
    uint64_t r10;        // [rsp + 0x28]
    uint64_t r9;         // [rsp + 0x30]  System V arg6
    uint64_t r8;         // [rsp + 0x38]  System V arg5
    uint64_t rbp;        // [rsp + 0x40]  Base pointer
    uint64_t rdi;        // [rsp + 0x48]  System V arg1
    uint64_t rsi;        // [rsp + 0x50]  System V arg2
    uint64_t rdx;        // [rsp + 0x58]  System V arg3
    uint64_t rcx;        // [rsp + 0x60]  System V arg4
    uint64_t rbx;        // [rsp + 0x68]
    uint64_t rax;        // [rsp + 0x70]  pushed first -> highest reg addr

    // pushed by stub before jmp isr_common_stub
    uint64_t int_no;     // [rsp + 0x78]  interrupt vector (0-31)
    uint64_t err_code;   // [rsp + 0x80]  CPU error code or dummy 0

    // pushed by CPU on exception entry (IRET frame)
    uint64_t rip;        // [rsp + 0x88]  faulting instruction
    uint64_t cs;         // [rsp + 0x90]  code segment
    uint64_t rflags;     // [rsp + 0x98]  CPU flags

    // only present on privilege-level change (ring3 -> ring0)
    uint64_t rsp;        // [rsp + 0xA0]  stack pointer before exception
    uint64_t ss;         // [rsp + 0xA8]  stack segment
} __attribute__((packed)) registers_t;

#endif //  KERNEL_ISR_H
