; CPUID availability check via EFLAGS ID bit (bit 21).
; On 8086/286 this bit is fixed to 0. On 386+ it's always 1 (no toggle).
; Only 486+ with CPUID support allows toggling it.

EFLAGS_ID equ 1 << 21        ; EFLAGS bit 21 = ID (CPUID support flag)

check_cpuid:
    ; Read current EFLAGS into eax
    pushfd                    ; push EFLAGS
    pop eax                   ; eax = current EFLAGS

    ; Save original value in ecx for later comparison
    mov ecx, eax              ; ecx = original EFLAGS
    ; Flip the ID bit
    xor eax, EFLAGS_ID        ; eax = EFLAGS with ID bit toggled

    ; Attempt to write flipped value to EFLAGS
    push eax                  ; push modified value
    popfd                     ; try to write it back to EFLAGS

    ; Read back EFLAGS after the attempted write
    pushfd                    ; push (possibly modified) EFLAGS
    pop eax                   ; eax = read-back value

    ; Restore original EFLAGS
    push ecx                  ; push original value
    popfd                     ; EFLAGS back to normal

    ; If bit 21 actually changed, CPU supports CPUID
    ; eax (read-back) ^ ecx (original): non-zero = bit toggled
    xor eax, ecx              ; did the write stick?
    jnz .supports_id          ; bit changed → CPUID supported

.not_supports_id:
    mov eax, 0                ; return 0: no CPUID
    ret

.supports_id:
    mov ax, 1                 ; return 1: CPUID supported
    ret

CPUID_EXTENSIONS      equ 0x80000000 ; returns the maximum extended requests for cpuid
CPUID_EXT_FEATURES    equ 0x80000001 ; returns flags containing long mode support among other things
CPUID_EDX_EXT_FEAT_LM equ 1 << 29    ; if this is set, the CPU supports long mode

check_long_mode:
    call check_cpuid          ; eax = 1 if CPUID available
    test eax, eax
    jz .no_lm                 ; CPUID not even available → no long mode

    mov eax, CPUID_EXTENSIONS ; leaf 0x80000000: max extended leaf
    cpuid                     ; answer in eax
    cmp eax, CPUID_EXT_FEATURES ; is leaf 0x80000001 available?
    jb .no_lm                 ; extended function 0x80000001 not supported

    mov eax, CPUID_EXT_FEATURES ; leaf 0x80000001: extended feature flags
    cpuid                     ; answer in edx
    test edx, CPUID_EDX_EXT_FEAT_LM ; bit 29 = long mode support
    jz .no_lm                 ; bit clear → no long mode

    mov eax, 1                ; return 1: long mode supported
    ret

.no_lm:
    xor eax, eax              ; return 0: no long mode
    ret
