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
;  5. Build page tables (identity-map 0-4 MiB), enable paging via CR0.PG.
;  6. Parse the kernel ELF: walk program headers, copy PT_LOAD segments,
;     zero the .bss, then jump to the entry point.

[BITS 16]
[ORG 0x8000]

stage2_start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00             ; stack grows down below stage 2

    mov [drive_num], dl        ; BIOS drive number (passed by stage 1)

    ; ---- 1. Load kernel ELF from disk ----
    mov si, dap
    mov ah, 0x42
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

; DAP — kernel ELF at LBA 9, 64 KiB → physical 0x10000
dap:
    db 0x10
    db 0
    dw 128                     ; 128 sectors (64 KiB)
    dw 0x0000                  ; buffer offset
    dw 0x1000                  ; buffer segment → 0x1000:0x0000
    dq 9                       ; starting LBA

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

    ; ---------- 5. paging: identity-map first 4 MiB ----------
    ;
    ; Each page-table entry (PTE) maps one 4 KiB page:
    ;   bits 31:12 = physical page address (4 KiB aligned)
    ;   bits 11:2  = 0 (reserved / available to OS)
    ;   bit  1     = Writable
    ;   bit  0     = Present (1 = page exists in memory)
    ;
    ; Each page-directory entry (PDE) points to a page table:
    ;   bits 31:12 = page-table address (4 KiB aligned)
    ;   bits 11:2  = 0
    ;   bit  1     = Writable
    ;   bit  0     = Present
    ;
    ; We place the Page Directory at 0x9000 and one Page Table at 0xA000.
    ; Together they map virtual 0x000000-0x3FFFFF to the same physical range.

    ; Clear PD (1024 PDEs, 4 bytes each)
    mov  edi, 0x9000
    mov  ecx, 1024
    xor  eax, eax
    rep  stosd

    ; Clear PT (1024 PTEs, 4 bytes each)
    mov  edi, 0xA000
    mov  ecx, 1024
    rep  stosd

    ; Fill PT: map physical 0x0, 0x1000, 0x2000, ... 0x3FF000
    mov  edi, 0xA000
    mov  eax, 0x00000003          ; page 0x00000, Present + Writable
    mov  ecx, 1024

.pt_fill:
    mov  [edi], eax
    add  edi, 4
    add  eax, 0x1000
    loop .pt_fill

    ; PD[0] → PT at 0xA000
    mov  dword [0x9000], 0xA003

    ; CR3 holds the physical address of the Page Directory.
    ; The CPU walks: CR3 -> PDE -> PTE -> physical page.
    mov  eax, 0x9000
    mov  cr3, eax

    ; CR0.PG (bit 31) enables paging.  CR0.PE (bit 0) is already set.
    ; After this instruction, every address is virtual and goes through
    ; the page tables.  Because we identity-mapped 0-4 MiB, existing
    ; code and data continue to work at the same addresses.
    mov  eax, cr0
    or   eax, 0x80000000
    mov  cr0, eax

    ; ---------- 6. parse kernel ELF at 0x10000 ----------
    ;
    ; ELF32 header layout (bytes 0-52):
    ;   0x00: e_ident[16]  (magic: 7F 45 4C 46 = ".ELF")
    ;   0x10: e_type[2]
    ;   0x12: e_machine[2]  (3 = EM_386)
    ;   0x18: e_entry[4]    (virtual address of _start)
    ;   0x1C: e_phoff[4]    (offset to program header table, in bytes)
    ;   0x2A: e_phentsize[2] (size of one program header, always 32)
    ;   0x2C: e_phnum[2]     (number of program headers)
    ;
    ; ELF32 program header layout (32 bytes each):
    ;   0x00: p_type[4]   (1 = PT_LOAD = loadable segment)
    ;   0x04: p_offset[4] (offset of segment data in the file)
    ;   0x0C: p_paddr[4]  (physical address to load segment at)
    ;   0x10: p_filesz[4] (size of segment data in the file)
    ;   0x14: p_memsz[4]  (size of segment in memory, .bss extends past p_filesz)

    mov  esi, 0x10000

    ; Verify ELF magic
    cmp  dword [esi], 0x464C457F   ; "\x7FELF"
    jne  elf_err
    cmp  byte [esi + 4], 1         ; ELFCLASS32
    jne  elf_err
    cmp  byte [esi + 5], 1         ; little-endian
    jne  elf_err

    ; Read entry point and program-header table
    mov  eax, [esi + 0x18]         ; e_entry (virtual address of kernel_main)
    mov  ebx, [esi + 0x1C]         ; e_phoff (byte offset to program headers)
    add  ebx, 0x10000              ; turn file offset into linear address
    movzx ecx, word [esi + 0x2A]   ; e_phentsize (size of each program header)
    movzx edx, word [esi + 0x2C]   ; e_phnum (how many program headers)

    push eax                       ; save entry point

.ph_loop:
    test edx, edx
    jz   .ph_done
    dec  edx

    cmp  dword [ebx], 1            ; PT_LOAD
    jne  .ph_next

    push esi
    push ecx

    mov  esi, [ebx + 0x04]         ; p_offset: where segment data starts in the file
    add  esi, 0x10000              ; source = ELF base + p_offset
    mov  edi, [ebx + 0x0C]         ; p_paddr: physical address to load at (e.g. 0x100000)
    mov  ecx, [ebx + 0x10]         ; p_filesz: bytes to copy from the ELF file

    cld
    rep  movsb                     ; copy .text + .rodata + .data into place

    ; BSS lives right after the copied data and must be zeroed.
    ; p_memsz >= p_filesz; the extra bytes are uninitialized (zero) data.
    mov  ecx, [ebx + 0x14]         ; p_memsz
    sub  ecx, [ebx + 0x10]         ; p_filesz -> length of .bss
    jz   .ph_copy_done
    mov  al, 0
    rep  stosb

.ph_copy_done:
    pop  ecx
    pop  esi

.ph_next:
    add  ebx, ecx                  ; next program header
    jmp  .ph_loop

.no_long_mode:
    mov  byte [0xB8000], 'C'       ; 'C' = CPU/long-mode error
    mov  byte [0xB8001], 0x4F
    cli
    hlt
    jmp  $

.ph_done:
    pop  eax                       ; entry point

    call eax                       ; jump to kernel

    cli
    hlt
    jmp  $

elf_err:
    mov  byte [0xB8000], 'L'       ; 'L' for Loader error
    mov  byte [0xB8001], 0x4F
    jmp  $
