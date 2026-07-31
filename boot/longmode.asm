; Long mode entry — disable 32-bit paging, build 4-level PAE page tables
; with a 2 MiB huge page, set EFER.LME, re-enable paging, then far jump
; into 64-bit long mode.
;
; Called from 32-bit protected mode.  Never returns.

; ---- Page-table addresses ----
PML4_ADDR   equ 0x9000
PDPT_ADDR   equ 0xA000
PD_ADDR     equ 0xB000

; ---- Entry flags ----
PT_PRESENT   equ 1 << 0
PT_WRITABLE  equ 1 << 1
PT_HUGE      equ 1 << 7           ; Page Size — 2 MiB page at PD level

; ---- Control-register bits ----
CR0_PG  equ 1 << 31
CR4_PAE equ 1 << 5

; ---- MSR addresses / bits ----
EFER_MSR equ 0xC0000080
EFER_LME equ 1 << 8

; ---- GDT selectors ----
GDT_CODE64 equ 0x18
GDT_DATA   equ 0x10

; ---- Kernel entry point (set by ELF64 parser in loader.asm) ----
kernel_entry: dq 0

; ===============================================================
; 32-bit section: build tables, enable paging, far jump to 64-bit.
; ===============================================================
[BITS 32]

enter_long_mode:
    ; --- 1. Disable paging (if active) ---
    mov  eax, cr0
    and  eax, ~CR0_PG
    mov  cr0, eax

    ; --- 2. Build 4-level page tables ---
    ; Clear PML4
    mov  edi, PML4_ADDR
    mov  ecx, 4096 / 4
    xor  eax, eax
    rep  stosd

    ; Clear PDPT
    mov  edi, PDPT_ADDR
    mov  ecx, 4096 / 4
    rep  stosd

    ; Clear PD
    mov  edi, PD_ADDR
    mov  ecx, 4096 / 4
    rep  stosd

    ; --- 3. Link: PML4[0] → PDPT → PD → 2 MiB huge page @ phys 0 ---
    mov  dword [PML4_ADDR],     PDPT_ADDR | PT_PRESENT | PT_WRITABLE
    mov  dword [PML4_ADDR + 4], 0

    mov  dword [PDPT_ADDR],     PD_ADDR | PT_PRESENT | PT_WRITABLE
    mov  dword [PDPT_ADDR + 4], 0

    mov  dword [PD_ADDR],       PT_PRESENT | PT_WRITABLE | PT_HUGE
    mov  dword [PD_ADDR + 4],   0

    ; --- 4. CR3 → PML4 ---
    mov  eax, PML4_ADDR
    mov  cr3, eax

    ; --- 5. CR4.PAE ---
    mov  eax, cr4
    or   eax, CR4_PAE
    mov  cr4, eax

    ; --- 6. EFER.LME ---
    mov  ecx, EFER_MSR
    rdmsr
    or   eax, EFER_LME
    wrmsr

    ; --- 7. Enable paging → compatibility mode ---
    mov  eax, cr0
    or   eax, CR0_PG
    mov  cr0, eax

    ; --- 8. Far jump → 64-bit long mode ---
    jmp  GDT_CODE64:long_mode_entry

; ===============================================================
; 64-bit entry — we are now in full 64-bit long mode.
; ===============================================================
[BITS 64]

long_mode_entry:
    ; Load data segments
    mov  ax, GDT_DATA
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    ; Load 64-bit kernel entry point.
    ; Avoid NASM RIP-relative [label] — use immediate address + deref.
    mov  eax, kernel_entry          ; EAX = address of kernel_entry (imm32)
    mov  rax, [rax]                 ; RAX = 64-bit kernel entry point
    jmp  rax                        ; → 64-bit kernel, never returns
