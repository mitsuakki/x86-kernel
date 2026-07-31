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
    xor eax, EFLAGS_ID

    ; Attempt to write flipped value to EFLAGS
    push eax
    popfd

    ; Read back EFLAGS after the attempted write
    pushfd
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

CPUID_EXTENSIONS      equ 0x80000000 ; returns the maximum extended requests for cpuid
CPUID_EXT_FEATURES    equ 0x80000001 ; returns flags containing long mode support among other things
CPUID_EDX_EXT_FEAT_LM equ 1 << 29    ; if this is set, the CPU supports long mode

check_long_mode:
    call check_cpuid
    test eax, eax
    jz .no_lm              ; CPUID not even available

    mov eax, CPUID_EXTENSIONS
    cpuid
    cmp eax, CPUID_EXT_FEATURES
    jb .no_lm              ; extended function 0x80000001 not supported

    mov eax, CPUID_EXT_FEATURES
    cpuid
    test edx, CPUID_EDX_EXT_FEAT_LM
    jz .no_lm

    mov eax, 1
    ret

.no_lm:
    xor eax, eax
    ret