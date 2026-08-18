; ===============================================================
; A20 enable — full recommended chain
;
; Tests A20 first.  If already on, returns immediately.
; Tries methods from safest → riskiest:
;   1. BIOS INT 0x15  (AX=0x2401)
;   2. Keyboard controller  (8042)
;   3. Fast A20 Gate  (port 0x92)
; ===============================================================

enable_a20:
    pusha                    ; save all 16-bit GPRs (we use ax)

    ; --- Already enabled? ---
    call check_a20           ; ax = 1 if A20 on, 0 if off
    test ax, ax
    jnz  .done               ; A20 already on: nothing to do

    ; --- Method 1: BIOS INT 0x15 ---
    mov  ax, 0x2401          ; AX=0x2401 = "enable A20" BIOS function
    int  0x15                ; ask the BIOS to enable A20
    call check_a20           ; verify it worked
    test ax, ax
    jnz  .done

    ; --- Method 2: Keyboard controller (8042) ---
    call enable_a20_kbd      ; set A20 via the 8042 output port
    call check_a20           ; verify it worked
    test ax, ax
    jnz  .done

    ; --- Method 3: Fast A20 Gate (port 0x92) ---
    in   al, 0x92            ; read current state of the fast A20 port
    test al, 2               ; is bit 1 (A20) already set?
    jnz  .fast_done          ; yes: nothing to write
    or   al, 2               ; no: set bit 1 (A20)
    and  al, 0xFE            ; keep bit 0 clear (avoid fast reset)
    out  0x92, al            ; write back - A20 enabled

.fast_done:
    ; Final check (optional — if still off, nothing more we can do)
    call check_a20           ; one last verification (result ignored)

.done:
    popa                     ; restore GPRs
    ret

; ---------------------------------------------------------------
; check_a20 — returns ax=1 if A20 enabled, ax=0 if disabled
; Preserves all registers except ax.
; ---------------------------------------------------------------
check_a20:
    push ds                  ; save registers we are about to touch
    push es
    push si
    push di
    cli                      ; no interrupts while memory is probed

    xor  ax, ax              ; ax = 0
    mov  ds, ax              ; ds = 0x0000
    not  ax                  ; ax = 0xFFFF
    mov  es, ax              ; es = 0xFFFF

    ; Save original bytes
    mov  al, [ds:0x0500]     ; byte at 0x0000:0x0500
    mov  ah, [es:0x0510]     ; byte at 0xFFFF:0x0510 (aliases the byte above if A20 off)
    push ax                  ; [sp] = original bytes

    ; Write different values
    mov  byte [ds:0x0500], 0x00 ; store 0x00 through ds
    mov  byte [es:0x0510], 0xFF ; store 0xFF through es

    ; Read back: if A20 off, [0x0000:0x0500] aliases to same
    ;            physical byte as [0xFFFF:0x0510]
    cmp  byte [ds:0x0500], 0xFF ; 0xFF read back? → wraparound → A20 off
    mov  ax, 1               ; assume enabled
    jne  .enabled            ; read back 0x00 → no alias → A20 on
    xor  ax, ax              ; alias happened → A20 off

.enabled:
    ; Restore original bytes
    pop  ax                  ; al/ah = original bytes
    mov  [ds:0x0500], al     ; restore through ds
    mov  [es:0x0510], ah     ; restore through es

    pop  di                  ; restore GPRs (reverse push order)
    pop  si
    pop  es
    pop  ds
    ret

; ---------------------------------------------------------------
; enable_a20_kbd — enable A20 via 8042 keyboard controller
; ---------------------------------------------------------------
enable_a20_kbd:
    cli                      ; no interrupts during the 8042 dialogue

    call .wait_in            ; wait until the 8042 input buffer is free
    mov  al, 0xAD            ; command: disable keyboard
    out  0x64, al            ; send to 8042 command port

    call .wait_in
    mov  al, 0xD0            ; command: read controller output port
    out  0x64, al

    call .wait_out           ; wait until the answer is in the output buffer
    in   al, 0x60            ; read the current output port value
    push ax                  ; keep it aside

    call .wait_in
    mov  al, 0xD1            ; command: write controller output port
    out  0x64, al

    call .wait_in            ; wait until we can send the data byte
    pop  ax                  ; current output port value back into ax
    or   al, 2               ; set bit 1 = A20 gate
    out  0x60, al            ; write new output port value

    call .wait_in
    mov  al, 0xAE            ; command: enable keyboard
    out  0x64, al

    call .wait_in            ; let the last command settle
    ret

.wait_in:                      ; wait until input buffer empty (bit 1 = 0)
    in   al, 0x64              ; read 8042 status
    test al, 2                 ; bit 1 = input buffer full?
    jnz  .wait_in              ; still full: spin
    ret

.wait_out:                     ; wait until output buffer full (bit 0 = 1)
    in   al, 0x64              ; read 8042 status
    test al, 1                 ; bit 0 = output buffer full?
    jz   .wait_out             ; still empty: spin
    ret
