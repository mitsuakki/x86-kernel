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
;  4. Build page tables (identity-map 0-4 MiB), enable paging via CR0.PG.
;  5. Parse the kernel ELF: walk program headers, copy PT_LOAD segments,
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

    ; ---- 1. Load kernel ELF while still in real mode ----
    mov si, dap2
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
dap2:
    db 0x10
    db 0
    dw 128                     ; 128 sectors (64 KiB)
    dw 0x0000                  ; buffer offset
    dw 0x1000                  ; buffer segment → 0x1000:0x0000
    dq 9                       ; starting LBA

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

; ===============================================================
; GDT (included here so labels resolve inside this binary)
; ===============================================================
%include "boot/gdt.asm"

; ===============================================================
; 32-bit Protected Mode
; ===============================================================
[BITS 32]

pmode_entry:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x90000

    ; ---------- paging: identity-map first 4 MiB ----------
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

    ; ---------- parse kernel ELF at 0x10000 ----------
    ;
    ; ELF32 header layout (bytes 0-52):
    ;   0x00: e_ident[16]  (magic: 7F 45 4C 46 = ".ELF")
    ;   0x10: e_type[2]
    ;   0x12: e_machine[2]  (3 = EM_386)
    ;   0x18: e_entry[4]    (virtual address of _start)
    ;   0x1C: e_phoff[4]    (offset to program header table, in bytes)
    ;   0x2C: e_phentsize[2] (size of one program header, always 32)
    ;   0x2E: e_phnum[2]     (number of program headers)
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
    movzx ecx, word [esi + 0x2C]   ; e_phentsize (size of each program header)
    movzx edx, word [esi + 0x2E]   ; e_phnum (how many program headers)

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
