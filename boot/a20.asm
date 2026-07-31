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
    pusha

    ; --- Already enabled? ---
    call check_a20
    test ax, ax
    jnz  .done

    ; --- Method 1: BIOS INT 0x15 ---
    mov  ax, 0x2401
    int  0x15
    call check_a20
    test ax, ax
    jnz  .done

    ; --- Method 2: Keyboard controller (8042) ---
    call enable_a20_kbd
    call check_a20
    test ax, ax
    jnz  .done

    ; --- Method 3: Fast A20 Gate (port 0x92) ---
    in   al, 0x92
    test al, 2
    jnz  .fast_done             ; already set
    or   al, 2
    and  al, 0xFE               ; keep bit 0 clear (avoid fast reset)
    out  0x92, al

.fast_done:
    ; Final check (optional — if still off, nothing more we can do)
    call check_a20

.done:
    popa
    ret

; ---------------------------------------------------------------
; check_a20 — returns ax=1 if A20 enabled, ax=0 if disabled
; Preserves all registers except ax.
; ---------------------------------------------------------------
check_a20:
    push ds
    push es
    push si
    push di
    cli

    xor  ax, ax
    mov  ds, ax                 ; ds = 0x0000
    not  ax
    mov  es, ax                 ; es = 0xFFFF

    ; Save original bytes
    mov  al, [ds:0x0500]
    mov  ah, [es:0x0510]
    push ax                     ; [sp] = original bytes

    ; Write different values
    mov  byte [ds:0x0500], 0x00
    mov  byte [es:0x0510], 0xFF

    ; Read back: if A20 off, [0x0000:0x0500] aliases to same
    ;            physical byte as [0xFFFF:0x0510]
    cmp  byte [ds:0x0500], 0xFF  ; equal → wraparound → A20 off
    mov  ax, 1
    jne  .enabled                 ; not equal → A20 on
    xor  ax, ax                   ; equal → A20 off

.enabled:
    ; Restore original bytes
    pop  ax
    mov  [ds:0x0500], al
    mov  [es:0x0510], ah

    pop  di
    pop  si
    pop  es
    pop  ds
    ret

; ---------------------------------------------------------------
; enable_a20_kbd — enable A20 via 8042 keyboard controller
; ---------------------------------------------------------------
enable_a20_kbd:
    cli

    call .wait_in               ; disable keyboard
    mov  al, 0xAD
    out  0x64, al

    call .wait_in               ; read controller output port
    mov  al, 0xD0
    out  0x64, al

    call .wait_out              ; get current value
    in   al, 0x60
    push ax

    call .wait_in               ; write controller output port
    mov  al, 0xD1
    out  0x64, al

    call .wait_in               ; set bit 1 (A20) in output port
    pop  ax
    or   al, 2
    out  0x60, al

    call .wait_in               ; re-enable keyboard
    mov  al, 0xAE
    out  0x64, al

    call .wait_in
    ret

.wait_in:                       ; wait until input buffer empty (bit 1 = 0)
    in   al, 0x64
    test al, 2
    jnz  .wait_in
    ret

.wait_out:                      ; wait until output buffer full (bit 0 = 1)
    in   al, 0x64
    test al, 1
    jz   .wait_out
    ret
