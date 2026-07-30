; CPUID availability check via EFLAGS ID bit (bit 21).
; On 8086/286 this bit is fixed to 0. On 386+ it's always 1 (no toggle).
; Only 486+ with CPUID support allows toggling it.

EFLAGS_ID equ 1 << 21

check_cpuid:
    ; Read current EFLAGS into eax
    pushfd
    pop eax

    ; Save original value in ecx for later comparison
    mov ecx, eax
    ; Flip the ID bit
    xor ecx, EFLAGS_ID

    ; Attempt to write flipped value to EFLAGS
    push ecx
    popfd

    ; Read back EFLAGS after the attempted write
    pushf
    pop eax

    ; Restore original EFLAGS
    push ecx
    popfd

    ; If bit 21 actually changed, CPU supports CPUID
    ; eax (read-back) ^ ecx (original): non-zero = bit tog
    xor eax, ecx
    jnz .supports_id

.not_supports_id:
    mov eax, 0
    ret

.supports_id:
    mov ax, 1
    ret
