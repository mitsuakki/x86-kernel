; Exception stubs — one per CPU exception vector (0-31).
; Stub pushes dummy error code for exceptions that don't have one,
; pushes the interrupt number, then jumps to the common handler.
;
; Exception error code table: https://wiki.osdev.org/Exceptions
; Error codes: 8 (#DF), 10 (#TS), 11 (#NP), 12 (#SS), 13 (#GP),
;              14 (#PF), 17 (#AC), 21 (#CP), 29 (#VC), 30 (#SX)

extern exception_handler

; ---- Stub for exceptions WITHOUT error code ----
; CPU pushes: [RFLAGS, CS, RIP] (no error code).
; Push dummy 0 so stack layout matches err-code exceptions.
%macro exception_no_err_stub 1
exception_stub_%+%1:
    push 0              ; dummy error code
    push %1             ; interrupt number
    jmp exception_common_stub
%endmacro

; ---- Stub for exceptions WITH error code ----
; CPU pushes: [RFLAGS, CS, RIP, error_code].
; Error code already on stack, just push interrupt number.
%macro exception_err_stub 1
exception_stub_%+%1:
    push %1             ; interrupt number
    jmp exception_common_stub
%endmacro

; ---- Common handler: save regs -> call C -> restore -> iretq ----
exception_common_stub:
    ; Save GPRs.  Push order MUST match registers_t layout in exception.h:
    ; first pushed -> highest address -> last struct field.
    push rax
    push rbx
    push rcx ; fourth argument
    push rdx ; third  argument
    push rsi ; second argument
    push rdi ; first  argument
    push rbp ; base pointer
    push r8  ; fifth argument
    push r9  ; sixth argument
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Pass registers_t* in rdi (System V ABI first argument)
    mov rdi, rsp
    call exception_handler

    ; Restore GPRs: reverse order
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Pop interrupt number + error code (8 + 8 = 16 bytes)
    add rsp, 16
    iretq

; ---- Instantiate 32 stubs (one per vector) ----
exception_no_err_stub 0   ; #DE  Division Error
exception_no_err_stub 1   ; #DB  Debug
exception_no_err_stub 2   ; #NMI Non-maskable Interrupt
exception_no_err_stub 3   ; #BP  Breakpoint
exception_no_err_stub 4   ; #OF  Overflow
exception_no_err_stub 5   ; #BR  Bound Range Exceeded
exception_no_err_stub 6   ; #UD  Invalid Opcode
exception_no_err_stub 7   ; #NM  Device Not Available
exception_err_stub    8   ; #DF  Double Fault (error = 0)
exception_no_err_stub 9   ; #CSO Coprocessor Segment Overrun
exception_err_stub    10  ; #TS  Invalid TSS
exception_err_stub    11  ; #NP  Segment Not Present
exception_err_stub    12  ; #SS  Stack-Segment Fault
exception_err_stub    13  ; #GP  General Protection Fault
exception_err_stub    14  ; #PF  Page Fault
exception_no_err_stub 15  ;      Reserved
exception_no_err_stub 16  ; #MF  x87 Floating-Point Exception
exception_err_stub    17  ; #AC  Alignment Check (error = 0)
exception_no_err_stub 18  ; #MC  Machine Check
exception_no_err_stub 19  ; #XM  SIMD Floating-Point Exception
exception_no_err_stub 20  ; #VE  Virtualization Exception
exception_err_stub    21  ; #CP  Control Protection Exception
exception_no_err_stub 22  ;      Reserved
exception_no_err_stub 23  ;      Reserved
exception_no_err_stub 24  ;      Reserved
exception_no_err_stub 25  ;      Reserved
exception_no_err_stub 26  ;      Reserved
exception_no_err_stub 27  ;      Reserved
exception_no_err_stub 28  ; #HV  Hypervisor Injection Exception
exception_err_stub    29  ; #VC  VMM Communication Exception
exception_err_stub    30  ; #SX  Security Exception
exception_no_err_stub 31  ;      Reserved

; ---- Stub address table: C reads this to fill the IDT ----
; dq = 64-bit addresses, matching uintptr_t[] stride on C side.
global exception_stub_table
exception_stub_table:
%assign i 0
%rep    32
    dq exception_stub_%+i
%assign i i+1
%endrep

extern irq_handler
extern pic_send_eoi

; ---- Stub for IRQs (no error code) ----
%macro irq_stub 1
irq_stub_%+%1:
    push 0              ; dummy error code
    push %1             ; IRQ vector (32-47)
    jmp irq_common_stub
%endmacro

; ---- IRQ common handler: save -> call C -> EOI -> restore -> iretq ----
irq_common_stub:
    push rax
    push rbx
    push rcx ; fourth argument
    push rdx ; third  argument
    push rsi ; second argument
    push rdi ; first  argument
    push rbp ; base pointer
    push r8  ; fifth argument
    push r9  ; sixth argument
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Pass registers_t* in rdi (System V ABI first argument)
    mov rdi, rsp
    call irq_handler

    ; pic_send_eoi(irq): int_no is at rsp + offset
    mov edi, [rsp + 0x78]  ; int_no offset in registers_t
    call pic_send_eoi

    ; Restore GPRs: reverse order
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq

; ---- Instantiate IRQ stubs 32-47 ----
irq_stub 32
irq_stub 33
irq_stub 34
irq_stub 35
irq_stub 36
irq_stub 37
irq_stub 38
irq_stub 39
irq_stub 40
irq_stub 41
irq_stub 42
irq_stub 43
irq_stub 44
irq_stub 45
irq_stub 46
irq_stub 47

global irq_stub_table
irq_stub_table:
%assign i 32
%rep    16
    dq irq_stub_%+i
%assign i i+1
%endrep
