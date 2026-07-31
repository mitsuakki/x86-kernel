; Loader (stage 2) - loaded at 0x8000 by the bootsector.
;
; We start in 16-bit real mode, same as the bootsector.  This is the last
; code that can use BIOS services (INT 0x13 for disk I/O).  Once we enter
; protected mode, the BIOS interrupt vector table is invalid.  No going back.
;
; Order of operations:
;  1. Load kernel ELF from disk while BIOS is still available.
;  2. Enable A20 gate.  Without this, every 1 MiB boundary wraps to 0.
;  3. Load a GDT and set CR0.PE to enter 32-bit protected mode.
;  4. Check CPUID + long mode support.
;  5. Enter long mode: build 4-level PAE page tables, enable PAE,
;     set EFER.LME, re-enable paging, far jump to 64-bit code.

[BITS 16]
[ORG 0x8000]

stage2_start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00             ; stack grows down below stage 2

    mov [drive_num], dl        ; BIOS drive number (passed by stage 1)

    ; ---- 1. Load kernel ELF from LBA 9 (CHS: cylinder 0, head 0, sector 10) ----
    mov ax, 0x1000
    mov es, ax          ; ES = 0x1000
    mov bx, 0x0000      ; ES:BX = 0x1000:0x0000 = physical 0x10000
    mov ah, 0x02        ; CHS read
    mov al, 10          ; 10 sectors (5120 bytes)
    mov ch, 0           ; cylinder 0
    mov cl, 10          ; sector 10 (LBA 9 → (9 % 18) + 1 = 10)
    mov dh, 0           ; head 0
    mov dl, [drive_num]
    int 0x13
    jc  disk_err

    ; ---- 2. Enable A20 (full fallback chain) ----
    call enable_a20

    cli

    ; ---- 3. Load GDT, enter protected mode ----
    lgdt [gdt_descriptor]

    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax

    jmp  0x08:pmode_entry

disk_err:
    mov  al, '2'               ; '2' = stage-2 disk error
    mov  ah, 0x0E
    int  0x10
    jmp  $

drive_num: db 0
kernel_entry: dq 0               ; 64-bit kernel entry point, set by ELF64 parser

; Kernel loaded via CHS read — DAP not needed for floppy.

; ===============================================================
; 16-bit includes (must assemble in [BITS 16] context)
; ===============================================================
%include "boot/a20.asm"
%include "boot/gdt.asm"

; ===============================================================
; 32-bit Protected Mode
; ===============================================================
[BITS 32]

; 32-bit includes (pushfd/popfd/32-bit regs need correct operand size)
%include "boot/cpuid.asm"
%include "boot/longmode.asm"

; longmode.asm ends in [BITS 64] — restore 32-bit for pmode_entry
[BITS 32]

pmode_entry:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x90000

    ; ---- 4. Check CPUID + long mode support ----
    call check_long_mode        ; returns eax=1 if supported
    test eax, eax
    jz   .no_long_mode

    ; ---- 5. Parse kernel ELF64 at 0x10000 ----
    mov  esi, 0x10000

    ; Verify ELF magic + class
    cmp  dword [esi], 0x464C457F   ; "\x7FELF"
    jne  elf_err
    cmp  byte [esi + 4], 2         ; ELFCLASS64
    jne  elf_err
    cmp  byte [esi + 5], 1         ; little-endian
    jne  elf_err

    ; Save 64-bit entry point (low 32 bits — kernel linked at 1 MiB)
    mov  eax, [esi + 0x18]         ; e_entry (low 32 bits)
    mov  [kernel_entry], eax
    mov  eax, [esi + 0x1C]         ; e_entry (high 32 bits)
    mov  [kernel_entry + 4], eax

    ; Walk program headers (ELF64: 56 bytes each)
    mov  eax, [esi + 0x20]         ; e_phoff (low 32 bits)
    add  eax, 0x10000              ; linear address
    mov  ebx, eax                  ; ebx = first PH address
    movzx ecx, word [esi + 0x36]   ; e_phentsize
    movzx edx, word [esi + 0x38]   ; e_phnum

.ph_loop:
    test edx, edx
    jz   .ph_done
    dec  edx

    cmp  dword [ebx], 1            ; PT_LOAD?
    jne  .ph_next

    push esi
    push ecx

    ; p_offset (low 32 bits) + ELF base → source
    mov  esi, [ebx + 0x08]         ; p_offset low
    add  esi, 0x10000
    ; p_paddr (low 32 bits) → destination
    mov  edi, [ebx + 0x18]         ; p_paddr low
    ; p_filesz (low 32 bits) → count
    mov  ecx, [ebx + 0x20]         ; p_filesz low

    cld
    rep  movsb                     ; copy segment

    ; Zero BSS (p_memsz - p_filesz)
    mov  ecx, [ebx + 0x28]         ; p_memsz low
    sub  ecx, [ebx + 0x20]         ; p_filesz low
    jz   .ph_copy_done
    mov  al, 0
    rep  stosb

.ph_copy_done:
    pop  ecx
    pop  esi

.ph_next:
    add  ebx, ecx                  ; next PH (ecx = phentsize)
    jmp  .ph_loop

.ph_done:
    ; ---- 6. Enter long mode → 64-bit kernel ----
    call enter_long_mode

    ; Safety net — should never reach here
    cli
    hlt
    jmp  $

.no_long_mode:
    mov  byte [0xB8000], 'C'       ; 'C' = CPU/long-mode error
    mov  byte [0xB8001], 0x4F
    cli
    hlt
    jmp  $

elf_err:
    mov  byte [0xB8000], 'L'       ; 'L' for Loader error
    mov  byte [0xB8001], 0x4F
    jmp  $
