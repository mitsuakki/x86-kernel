; ISR stubs — one per CPU exception vector (0-31).
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
%macro isr_no_err_stub 1
isr_stub_%+%1:
    push 0              ; dummy error code
    push %1             ; interrupt number
    jmp isr_common_stub
%endmacro

; ---- Stub for exceptions WITH error code ----
; CPU pushes: [RFLAGS, CS, RIP, error_code].
; Error code already on stack — just push interrupt number.
%macro isr_err_stub 1
isr_stub_%+%1:
    push %1             ; interrupt number
    jmp isr_common_stub
%endmacro

; ---- Common handler: save regs → call C → restore → iret ----
isr_common_stub:
    ; Save GPRs.  Push order MUST match registers_t layout in isr.h:
    ; first pushed → highest address → last struct field.
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Pass registers_t* in rdi (System V ABI first argument)
    mov rdi, rsp
    call exception_handler

    ; Restore GPRs — reverse order
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
isr_no_err_stub 0   ; #DE  Division Error
isr_no_err_stub 1   ; #DB  Debug
isr_no_err_stub 2   ;      Non-maskable Interrupt
isr_no_err_stub 3   ; #BP  Breakpoint
isr_no_err_stub 4   ; #OF  Overflow
isr_no_err_stub 5   ; #BR  Bound Range Exceeded
isr_no_err_stub 6   ; #UD  Invalid Opcode
isr_no_err_stub 7   ; #NM  Device Not Available
isr_err_stub    8   ; #DF  Double Fault (error = 0)
isr_no_err_stub 9   ;      Coprocessor Segment Overrun
isr_err_stub    10  ; #TS  Invalid TSS
isr_err_stub    11  ; #NP  Segment Not Present
isr_err_stub    12  ; #SS  Stack-Segment Fault
isr_err_stub    13  ; #GP  General Protection Fault
isr_err_stub    14  ; #PF  Page Fault
isr_no_err_stub 15  ;      Reserved
isr_no_err_stub 16  ; #MF  x87 Floating-Point Exception
isr_err_stub    17  ; #AC  Alignment Check (error = 0)
isr_no_err_stub 18  ; #MC  Machine Check
isr_no_err_stub 19  ; #XM  SIMD Floating-Point Exception
isr_no_err_stub 20  ; #VE  Virtualization Exception
isr_err_stub    21  ; #CP  Control Protection Exception
isr_no_err_stub 22  ;      Reserved
isr_no_err_stub 23  ;      Reserved
isr_no_err_stub 24  ;      Reserved
isr_no_err_stub 25  ;      Reserved
isr_no_err_stub 26  ;      Reserved
isr_no_err_stub 27  ;      Reserved
isr_no_err_stub 28  ; #HV  Hypervisor Injection Exception
isr_err_stub    29  ; #VC  VMM Communication Exception
isr_err_stub    30  ; #SX  Security Exception
isr_no_err_stub 31  ;      Reserved

; ---- Stub address table — C reads this to fill the IDT ----
; dq = 64-bit addresses, matching uintptr_t[] stride on C side.
global isr_stub_table
isr_stub_table:
%assign i 0
%rep    32
    dq isr_stub_%+i
%assign i i+1
%endrep
