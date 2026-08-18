; Long mode entry — disable 32-bit paging, build 4-level PAE page tables
; with a 2 MiB huge page, set EFER.LME, re-enable paging, then far jump
; into 64-bit long mode.
;
; Called from 32-bit protected mode.  Never returns.

; ---- Page-table addresses ----
PML4_ADDR   equ 0x9000       ; 4th-level table (PML4)
PDPT_ADDR   equ 0xA000       ; 3rd-level table (page-directory pointer)
PD_ADDR     equ 0xB000       ; 2nd-level table (page directory)

; ---- Entry flags ----
PT_PRESENT   equ 1 << 0      ; entry is present
PT_WRITABLE  equ 1 << 1      ; page is writable
PT_HUGE      equ 1 << 7      ; Page Size — 2 MiB page at PD level

; ---- Control-register bits ----
CR0_PG  equ 1 << 31          ; CR0 bit 31 = paging enable
CR4_PAE equ 1 << 5           ; CR4 bit 5 = physical address extension

; ---- MSR addresses / bits ----
EFER_MSR equ 0xC0000080      ; MSR index of EFER
EFER_LME equ 1 << 8          ; EFER bit 8 = long mode enable

; ---- GDT selectors ----
GDT_CODE64 equ 0x18          ; 64-bit code segment
GDT_DATA   equ 0x10          ; data segment

; ---- Kernel entry point (set by ELF64 parser in loader.s) ----
kernel_entry: dq 0           ; 64-bit entry address, filled by loader.s

; ---- Kernel BSS bounds (linker-defined symbols in kernel ELF) ----
; These are absolute addresses resolved at kernel link time.
; The ELF parser copies them from the kernel's symbol table.
__bss_start: dq 0            ; first byte of kernel BSS
__bss_end:   dq 0            ; one past the last byte of kernel BSS

; ===============================================================
; 32-bit section: build tables, enable paging, far jump to 64-bit.
; ===============================================================
[BITS 32]

enter_long_mode:
    ; --- 1. Disable paging (if active) ---
    mov  eax, cr0            ; read CR0
    and  eax, ~CR0_PG        ; clear PG (bit 31): disable paging
    mov  cr0, eax            ; paging off

    ; --- 2. Build 4-level page tables ---
    ; Clear PML4
    mov  edi, PML4_ADDR      ; destination: PML4 at 0x9000
    mov  ecx, 4096 / 4       ; 4096 bytes = 1024 dwords
    xor  eax, eax            ; fill value = 0
    rep  stosd               ; zero PML4

    ; Clear PDPT
    mov  edi, PDPT_ADDR      ; destination: PDPT at 0xA000
    mov  ecx, 4096 / 4       ; 1024 dwords (eax still 0)
    rep  stosd               ; zero PDPT

    ; Clear PD
    mov  edi, PD_ADDR        ; destination: PD at 0xB000
    mov  ecx, 4096 / 4       ; 1024 dwords
    rep  stosd               ; zero PD

    ; --- 3. Link: PML4[0] → PDPT → PD → 2 MiB huge page @ phys 0 ---
    mov  dword [PML4_ADDR],     PDPT_ADDR | PT_PRESENT | PT_WRITABLE ; PML4[0] → PDPT
    mov  dword [PML4_ADDR + 4], 0                                     ; PML4[0] high dword = 0

    mov  dword [PDPT_ADDR],     PD_ADDR | PT_PRESENT | PT_WRITABLE   ; PDPT[0] → PD
    mov  dword [PDPT_ADDR + 4], 0                                     ; PDPT[0] high dword = 0

    mov  dword [PD_ADDR],       PT_PRESENT | PT_WRITABLE | PT_HUGE   ; PD[0] → 2 MiB page at phys 0
    mov  dword [PD_ADDR + 4],   0                                    ; PD[0] high dword = 0

    ; --- 4. CR3 → PML4 ---
    mov  eax, PML4_ADDR      ; top-level table address
    mov  cr3, eax            ; CR3 points to the PML4

    ; --- 5. CR4.PAE ---
    mov  eax, cr4            ; read CR4
    or   eax, CR4_PAE        ; set PAE (bit 5): required for long mode
    mov  cr4, eax

    ; --- 6. EFER.LME ---
    mov  ecx, EFER_MSR       ; MSR 0xC0000080 = EFER
    rdmsr                    ; read EFER into edx:eax
    or   eax, EFER_LME       ; set LME (bit 8): long mode enable
    wrmsr                    ; write EFER back

    ; --- 7. Enable paging → compatibility mode ---
    mov  eax, cr0            ; read CR0
    or   eax, CR0_PG         ; set PG (bit 31): enable paging
    mov  cr0, eax            ; now in 32-bit compatibility mode

    ; --- 8. Far jump → 64-bit long mode ---
    jmp  GDT_CODE64:long_mode_entry ; far jump: CS=0x18 → 64-bit code

; ===============================================================
; 64-bit entry — we are now in full 64-bit long mode.
; ===============================================================
[BITS 64]

long_mode_entry:
    ; Load data segments
    mov  ax, GDT_DATA        ; data segment selector (0x10)
    mov  ds, ax              ; ds = flat data
    mov  es, ax              ; es = flat data
    mov  fs, ax              ; fs = flat data
    mov  gs, ax              ; gs = flat data
    mov  ss, ax              ; ss = flat data

    ; Zero kernel BSS using bounds saved by ELF64 parser.
    ; __bss_start / __bss_end are in this 64-bit section — no
    ; RIP-relative issue since we load addresses as immediates.
    mov  eax, __bss_start    ; EAX = address of the __bss_start variable
    mov  rdi, [rax]          ; RDI = BSS start (physical addr)
    mov  eax, __bss_end      ; EAX = address of the __bss_end variable
    mov  rcx, [rax]          ; RCX = BSS end
    sub  rcx, rdi            ; RCX = byte count to zero
    jz   .bss_done           ; nothing to zero: skip
    xor  eax, eax            ; fill value = 0
    rep  stosb               ; zero BSS (stosb: RDI += RCX, fill AL)
.bss_done:

    ; Align stack to 16 bytes — SysV AMD64 ABI requires RSP % 16 == 0
    ; before call.  RSP is currently 8 mod 16 (32-bit call left a
    ; 4-byte return address on the 64-bit stack).
    and  rsp, ~0xF           ; clear low 4 bits of RSP

    ; Load kernel entry and call — proper ABI boundary.
    mov  eax, kernel_entry   ; EAX = address of the kernel_entry variable
    mov  rax, [rax]          ; RAX = 64-bit kernel entry point
    call rax                 ; → kernel_main(), never returns
