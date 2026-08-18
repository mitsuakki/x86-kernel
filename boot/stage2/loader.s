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

[BITS 16]                    ; start in real mode
[ORG 0x8000]                 ; stage 1 loaded us at 0x8000

stage2_start:
    xor ax, ax               ; ax = 0
    mov ds, ax               ; data segment = 0
    mov es, ax               ; extra segment = 0
    mov ss, ax               ; stack segment = 0
    mov sp, 0x7C00           ; stack grows down below stage 2 (0x8000)

    mov [drive_num], dl      ; BIOS drive number (passed by stage 1)

    ; ---- 1. Load kernel ELF from LBA 9 (CHS: cylinder 0, head 0, sector 10) ----
    mov ax, 0x1000           ; segment 0x1000
    mov es, ax               ; ES = 0x1000
    mov bx, 0x0000           ; ES:BX = 0x1000:0x0000 = physical 0x10000
    mov ah, 0x02             ; BIOS read sectors function (CHS)
    mov al, 64               ; 64 sectors (32 KiB)
    mov ch, 0                ; cylinder 0
    mov cl, 10               ; sector 10 (LBA 9 → (9 % 18) + 1 = 10)
    mov dh, 0                ; head 0
    mov dl, [drive_num]      ; drive to read
    int 0x13                 ; BIOS disk service: read sectors
    jc  disk_err             ; carry set = read failed

    ; ---- 2. Enable A20 (full fallback chain) ----
    call enable_a20          ; A20 must be on to address beyond 1 MiB

    cli                      ; no more BIOS calls after this point

    ; ---- 3. Load GDT, enter protected mode ----
    lgdt [gdt_descriptor]    ; give the CPU our GDT

    mov  eax, cr0            ; read CR0
    or   eax, 1              ; set PE (bit 0) = protected mode
    mov  cr0, eax            ; we are now in 16-bit protected mode

    jmp  0x08:pmode_entry    ; far jump loads CS=0x08 → 32-bit code

disk_err:
    mov  al, '2'             ; '2' = stage-2 disk error
    mov  ah, 0x0E            ; BIOS teletype output
    int  0x10                ; BIOS video service: print char in AL
    jmp  $                   ; hang forever

drive_num: db 0              ; drive number saved at boot

; Kernel loaded via CHS read — DAP not needed for floppy.

; ===============================================================
; 16-bit includes (must assemble in [BITS 16] context)
; ===============================================================
%include "a20.s"             ; A20 enable routines (16-bit)
%include "gdt.s"             ; GDT data (16-bit)

; ===============================================================
; 32-bit Protected Mode
; ===============================================================
[BITS 32]                    ; from here on: 32-bit code

; 32-bit includes (pushfd/popfd/32-bit regs need correct operand size)
%include "cpuid.s"           ; CPUID + long-mode checks (32-bit)
%include "longmode.s"        ; long-mode entry (32-bit, ends in 64-bit)

; longmode.s ends in [BITS 64] — restore 32-bit for pmode_entry
[BITS 32]

pmode_entry:
    mov  ax, 0x10            ; data segment selector
    mov  ds, ax              ; ds = flat data
    mov  es, ax              ; es = flat data
    mov  fs, ax              ; fs = flat data
    mov  gs, ax              ; gs = flat data
    mov  ss, ax              ; ss = flat data
    mov  esp, 0x90000        ; 32-bit stack (above the page tables at 0x9000)

    ; ---- 4. Check CPUID + long mode support ----
    call check_long_mode     ; returns eax=1 if supported
    test eax, eax
    jz   .no_long_mode

    ; ---- 5. Parse kernel ELF64 at 0x10000 ----
    mov  esi, 0x10000        ; kernel ELF64 was loaded here

    ; Verify ELF magic + class
    cmp  dword [esi], 0x464C457F ; "\x7FELF" magic
    jne  elf_err             ; not an ELF → error
    cmp  byte [esi + 4], 2   ; ELFCLASS64?
    jne  elf_err
    cmp  byte [esi + 5], 1   ; little-endian?
    jne  elf_err

    ; Save 64-bit entry point (low 32 bits — kernel linked at 1 MiB)
    mov  eax, [esi + 0x18]   ; e_entry (low 32 bits)
    mov  [kernel_entry], eax ; save for the 64-bit jump
    mov  eax, [esi + 0x1C]   ; e_entry (high 32 bits)
    mov  [kernel_entry + 4], eax

    ; Walk program headers (ELF64: 56 bytes each)
    mov  eax, [esi + 0x20]   ; e_phoff (low 32 bits)
    add  eax, 0x10000        ; linear address of the first program header
    mov  ebx, eax            ; ebx = first PH address
    movzx ecx, word [esi + 0x36] ; e_phentsize (56 bytes)
    movzx edx, word [esi + 0x38] ; e_phnum

.ph_loop:
    test edx, edx            ; any headers left?
    jz   .ph_done            ; no: all segments copied
    dec  edx                 ; count this one

    cmp  dword [ebx], 1      ; p_type == PT_LOAD (loadable segment)?
    jne  .ph_next            ; no: skip it

    push esi                 ; save ELF base
    push ecx                 ; save phentsize

    ; p_offset (low 32 bits) + ELF base → source
    mov  esi, [ebx + 0x08]   ; p_offset (low 32 bits)
    add  esi, 0x10000        ; + ELF base → source linear address
    ; p_paddr (low 32 bits) → destination
    mov  edi, [ebx + 0x18]   ; p_paddr (low 32 bits)
    ; p_filesz (low 32 bits) → count
    mov  ecx, [ebx + 0x20]   ; p_filesz (low 32 bits)

    cld                      ; copy forward
    rep  movsb               ; copy the segment

    ; Zero BSS (p_memsz - p_filesz)
    mov  ecx, [ebx + 0x28]   ; p_memsz (low 32 bits)
    sub  ecx, [ebx + 0x20]   ; - p_filesz → BSS size
    jz   .ph_copy_done       ; no BSS for this segment
    mov  al, 0               ; fill value = 0
    rep  stosb               ; zero the BSS

    ; Save BSS bounds for this segment: start = paddr + filesz, end = paddr + memsz
    ; Stored at __bss_start / __bss_end in longmode.s for the 64-bit stub.
    mov  eax, [ebx + 0x18]   ; p_paddr (low 32 bits)
    add  eax, [ebx + 0x20]   ; + p_filesz → BSS start
    mov  [__bss_start], eax  ; save for the 64-bit stub
    mov  eax, [ebx + 0x18]   ; p_paddr (low 32 bits)
    add  eax, [ebx + 0x28]   ; + p_memsz → BSS end
    mov  [__bss_end], eax    ; save for the 64-bit stub

.ph_copy_done:
    pop  ecx                 ; restore phentsize
    pop  esi                 ; restore ELF base

.ph_next:
    add  ebx, ecx            ; next PH (ecx = phentsize)
    jmp  .ph_loop

.ph_done:
    ; ---- 6. Enter long mode → 64-bit kernel ----
    call enter_long_mode     ; switches to 64-bit, never returns

    ; Safety net — should never reach here
    cli                      ; disable interrupts
    hlt                      ; stop the CPU
    jmp  $                   ; hang forever

.no_long_mode:
    mov  byte [0xB8000], 'C' ; 'C' = CPU/long-mode error
    mov  byte [0xB8001], 0x4F ; attribute: white on red
    cli                      ; disable interrupts
    hlt                      ; stop the CPU
    jmp  $                   ; hang forever

elf_err:
    mov  byte [0xB8000], 'L' ; 'L' for Loader error
    mov  byte [0xB8001], 0x4F ; attribute: white on red
    jmp  $                   ; hang forever
