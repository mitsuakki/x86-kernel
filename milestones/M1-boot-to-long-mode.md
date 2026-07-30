# M1 — Boot to Long Mode

**Goal:** real mode → protected mode → long mode.  Minimal paging to enable it.
Text output via VGA working.

**Status:**
- [x] Real mode bootsector (stage 1)
- [x] A20 gate, GDT, protected mode switch (stage 2)
- [x] Identity paging (4 MiB, two-level)
- [x] ELF32 parser, kernel jump
- [ ] Long mode (IA-32e, 4-level paging)
- [X] VGA text output

---

## 1. BIOS brings us to life

When the PC powers on, the BIOS firmware runs a Power-On Self Test (POST), then
looks for a bootable disk.  A disk is bootable if its first sector (512 bytes) ends
with the bytes `0x55 0xAA`.  The BIOS loads that sector to physical address `0x7C00`
and jumps there.  The CPU is in **real mode**: 16-bit instructions, 1 MiB of
addressable memory, no memory protection.

**Key facts about real mode:**
- 20-bit address bus → 1 MiB addressable
- Address = `segment * 16 + offset` (e.g. `0x07C0:0x0000` = `0x7C00`)
- No privilege levels, no paging, no memory protection
- BIOS interrupt services available (INT 0x10 video, INT 0x13 disk)

**Further reading:** <https://wiki.osdev.org/Real_Mode>

---

## 2. Stage 1 — the bootsector (`boot/boot.asm`)

We are at `0x7C00` in real mode.  We have exactly 510 bytes of code and data before
the `0xAA55` signature.

First, clear the segment registers and save the boot drive number (BIOS passes it in
`DL`):

```nasm
[ORG 0x7C00]
[BITS 16]

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; stack grows down from our code

    mov [drive_num], dl     ; save BIOS drive number
```

### Loading stage 2 with BIOS Extended Read

We pass a **Disk Address Packet (DAP)** that describes the transfer:

```nasm
    mov si, dap             ; DS:SI points to the DAP
    mov ah, 0x42            ; BIOS Extended Read function
    mov dl, [drive_num]
    int 0x13                ; call BIOS disk service
    jc  disk_err            ; CF=1 means the read failed

    jmp 0x0000:0x8000       ; hand off to the loader
```

The DAP tells the BIOS we want 8 sectors starting at LBA 1, loaded to
`0x0000:0x8000`:

```nasm
dap:
    db 0x10                 ; packet size (16 bytes)
    db 0                    ; reserved, must be 0
    dw 8                    ; sector count (8 x 512 = 4 KiB)
    dw 0x8000               ; buffer offset
    dw 0x0000               ; buffer segment -> 0x0000:0x8000
    dq 1                    ; starting LBA (sector 1, right after us)
```

Finally, the bootsector must be exactly 512 bytes and end with the magic signature:

```nasm
times 510 - ($ - $$) db 0   ; pad with zeros to byte 510
dw 0xAA55                   ; boot signature at bytes 511-512
```

If the disk read fails, we print `'1'` via BIOS teletype output and halt:

```nasm
disk_err:
    mov al, '1'
    mov ah, 0x0E            ; BIOS Teletype Output
    int 0x10
    jmp $
```

**Further reading:** <https://wiki.osdev.org/Boot_Sequence>

---

## 3. Stage 2 (`boot/loader.asm`) — the bridge

We arrive at `0x8000`, still in real mode.  This stage does five things:

1. Load the kernel ELF from disk
2. Enable the A20 gate
3. Enter protected mode (GDT + PE bit)
4. Set up identity paging
5. Parse the ELF and jump to the kernel

### 3a. Load the kernel ELF from disk

We use BIOS INT 0x13 one more time.  This must happen **before** we enter protected
mode because BIOS interrupts are invalid in protected mode.  The kernel ELF starts at
LBA 9, and we read 128 sectors (64 KiB) to `0x10000`:

```nasm
[BITS 16]
[ORG 0x8000]

stage2_start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [drive_num], dl     ; drive number passed by stage 1

    mov si, dap2
    mov ah, 0x42
    mov dl, [drive_num]
    int 0x13
    jc  disk_err

; ...

dap2:
    db 0x10                 ; packet size
    db 0                    ; reserved
    dw 128                  ; 128 sectors (64 KiB)
    dw 0x0000               ; buffer offset
    dw 0x1000               ; buffer segment -> 0x1000:0x0000
    dq 9                    ; starting LBA
```

### 3b. Enable the A20 gate

The A20 address line is the 21st bit of the address bus (bit 20, counting from 0).
On the original IBM PC, this line was forced to 0 by a gate to mimic the 8086 memory
wraparound: address `0x10FFEF` would wrap to `0x0FFEF` because only 20 address lines
existed.  To access memory above 1 MiB, we must open this gate.

#### Testing if A20 is already enabled

```nasm
check_a20:
    push ds
    push es
    cli

    xor  ax, ax
    mov  ds, ax             ; ds = 0x0000
    not  ax
    mov  es, ax             ; es = 0xFFFF

    mov  byte [ds:0x0500], 0x00
    mov  byte [es:0x0510], 0xFF

    cmp  byte [ds:0x0500], 0xFF
    mov  ax, 1
    jne  .enabled           ; values differ -> A20 on (no wraparound)
    xor  ax, ax             ; values same -> A20 off (wraparound)
.enabled:
    pop  es
    pop  ds
    ret
```

The trick: physical address of `0x0000:0x0500` is `0x000500`.  Physical address of
`0xFFFF:0x0510` is `0x100510`.  But if A20 is forced to 0, the 21st bit is masked
and `0x100510` becomes `0x000510` — the same byte.  Writing different values and
reading back tells us if A20 is on.

#### Enabling A20 — four fallback methods

```nasm
enable_a20:
    pusha

    call check_a20              ; 0. already on?
    test ax, ax
    jnz  .done

    mov  ax, 0x2401             ; 1. try BIOS function
    int  0x15
    call check_a20
    test ax, ax
    jnz  .done

    call enable_a20_kbd         ; 2. try keyboard controller (8042)
    call check_a20
    test ax, ax
    jnz  .done

    in   al, 0x92               ; 3. last resort: Fast A20 Gate
    test al, 2
    jnz  .fast_done
    or   al, 2
    and  al, 0xFE               ; keep bit 0 clear (avoids CPU reset)
    out  0x92, al
.fast_done:
    call check_a20

.done:
    popa
    ret
```

#### Keyboard controller method (8042)

Port `0x64` is status/command, port `0x60` is data.  We disable the keyboard, read
the controller output port, set bit 1 (the A20 pin), write it back, and re-enable:

```nasm
enable_a20_kbd:
    cli

    call .wait_in               ; wait until input buffer empty
    mov  al, 0xAD               ; command: disable keyboard
    out  0x64, al

    call .wait_in
    mov  al, 0xD0               ; command: read output port
    out  0x64, al

    call .wait_out              ; wait until output buffer has data
    in   al, 0x60               ; read current output port value
    push ax

    call .wait_in
    mov  al, 0xD1               ; command: write output port
    out  0x64, al

    call .wait_in
    pop  ax
    or   al, 2                  ; set bit 1 (A20 gate)
    out  0x60, al               ; write new value

    call .wait_in
    mov  al, 0xAE               ; command: enable keyboard
    out  0x64, al

    call .wait_in
    ret
```

The `wait_in` / `wait_out` helpers poll the status register at port `0x64`:

```nasm
.wait_in:                       ; bit 1 = input buffer full (must be 0 to write)
    in   al, 0x64
    test al, 2
    jnz  .wait_in
    ret

.wait_out:                      ; bit 0 = output buffer full (must be 1 to read)
    in   al, 0x64
    test al, 1
    jz   .wait_out
    ret
```

**Further reading:** <https://wiki.osdev.org/A20_Line>

### 3c. Enter protected mode

Disable interrupts (`cli`), load the GDT, set the PE bit in CR0, and do a far jump
to flush the prefetch queue:

```nasm
    cli

    lgdt [gdt_descriptor]       ; load GDT register

    mov  eax, cr0
    or   eax, 1                 ; set PE (Protection Enable), bit 0
    mov  cr0, eax

    jmp  0x08:pmode_entry       ; far jump: loads CS with code selector (0x08)
```

#### The GDT (`boot/gdt.asm`)

The GDT defines three descriptors in a flat memory model.  Base address is 0, limit
is `0xFFFFF` pages of 4 KiB each (4 GiB total).

Each descriptor is 8 bytes:

```
bits 63:56 = base[31:24]
bits 55:52 = flags (Gr, Sz, 0, 0)
bits 51:48 = limit[19:16]
bits 47:40 = access byte (Present, DPL, S, Type[4])
bits 39:16 = base[23:0]
bits 15:0  = limit[15:0]
```

Access byte (bits 47:40):
| Bit | Name | Meaning when set |
|-----|------|-----------------|
| 7 | Present | Valid descriptor |
| 6-5 | DPL | Descriptor Privilege Level (0 = ring 0) |
| 4 | S | 1 = code/data segment, 0 = system |
| 3 | Type.E | 1 = executable (code), 0 = data |
| 2 | Type.DC | Data: direction; Code: conforming |
| 1 | Type.RW | Data: writable; Code: readable |
| 0 | Type.A | Accessed (CPU sets on first access) |

Flags (bits 55:52):
| Bit | Name | Meaning when set |
|-----|------|-----------------|
| 3 | Gr | Granularity: 0 = bytes, 1 = 4 KiB pages |
| 2 | Sz | Size: 0 = 16-bit, 1 = 32-bit protected |

```nasm
gdt_start:

; Null descriptor (selector 0x00) - required by the CPU
gdt_null:
    dd 0x0
    dd 0x0

; Code segment (selector 0x08)
gdt_code:
    dw 0xFFFF       ; limit[15:0]
    dw 0x0000       ; base[15:0]
    db 0x00         ; base[23:16]
    db 10011010b    ; access: Present, DPL=0, Executable, Readable
    db 11001111b    ; flags: Gr=1 (4 KiB granularity), Sz=1 (32-bit)
    db 0x00         ; base[31:24]

; Data segment (selector 0x10)
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b    ; access: Present, DPL=0, Data, Writable
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; GDT size minus 1
    dd gdt_start                ; linear address of GDT
```

After the far jump, we are in 32-bit protected mode.  Set all segment registers to
the data selector and establish a stack:

```nasm
[BITS 32]
pmode_entry:
    mov  ax, 0x10               ; data segment selector
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x90000           ; stack at ~576 KiB
```

**Further reading:** <https://wiki.osdev.org/Protected_Mode>, <https://wiki.osdev.org/GDT>

### 3d. Set up paging

We build a two-level page table at `0x9000`.  One Page Directory (1024 PDEs) and one
Page Table (1024 PTEs), identity-mapping the first 4 MiB.

**PTE format** (32-bit): bits 31:12 = physical page address; bit 1 = Writable;
bit 0 = Present.  The value `0x00000003` means "physical page 0, writable, present."

```nasm
    ; Clear Page Directory
    mov  edi, 0x9000
    mov  ecx, 1024
    xor  eax, eax
    rep  stosd

    ; Clear Page Table
    mov  edi, 0xA000
    mov  ecx, 1024
    rep  stosd

    ; Fill Page Table: map 1024 pages (4 KiB each = 4 MiB)
    mov  edi, 0xA000
    mov  eax, 0x00000003         ; page 0x00000, Present + Writable
    mov  ecx, 1024
.pt_fill:
    mov  [edi], eax
    add  edi, 4
    add  eax, 0x1000             ; next 4 KiB page
    loop .pt_fill

    ; Page Directory entry 0 points to the Page Table
    mov  dword [0x9000], 0xA003  ; PD[0] -> 0xA000, Present + Writable
```

Load the Page Directory address into CR3, then flip the PG bit in CR0:

```nasm
    mov  eax, 0x9000
    mov  cr3, eax                ; CR3 = physical address of Page Directory

    mov  eax, cr0
    or   eax, 0x80000000         ; set PG (Paging), bit 31
    mov  cr0, eax                ; paging is now active
```

After this instruction, every address goes through the page tables.  Because we
identity-mapped 0-4 MiB, virtual address X still equals physical address X.
Nothing breaks.

**Key registers in paging:**
| Register | Role |
|----------|------|
| CR0.PG (bit 31) | Enable paging |
| CR0.PE (bit 0) | Enable protected mode (must be set before PG) |
| CR3 | Physical address of Page Directory |

**Why identity-map?**  When we set CR0.PG, EIP still holds a physical address.  If we
didn't identity-map, the next instruction fetch would page-fault.

**Further reading:** <https://wiki.osdev.org/Paging>

### 3e. Parse the ELF and jump to the kernel

The kernel ELF sits at `0x10000` in memory.  We verify the magic bytes, then walk
the program header table.  For each `PT_LOAD` segment, copy its data to the target
physical address and zero the BSS.

**ELF32 header layout** (key fields):
| Offset | Size | Name | Meaning |
|--------|------|------|---------|
| 0x00 | 4 | e_ident[0:4] | Magic: `\x7FELF` |
| 0x04 | 1 | e_ident[4] | Class: 1 = 32-bit |
| 0x05 | 1 | e_ident[5] | Data: 1 = little-endian |
| 0x18 | 4 | e_entry | Entry point virtual address |
| 0x1C | 4 | e_phoff | Program header table offset |
| 0x2A | 2 | e_phentsize | Size of each program header |
| 0x2C | 2 | e_phnum | Number of program headers |

**Program header layout** (32-bit, 32 bytes each):
| Offset | Size | Name | Meaning |
|--------|------|------|---------|
| 0x00 | 4 | p_type | 1 = PT_LOAD |
| 0x04 | 4 | p_offset | Offset in file |
| 0x0C | 4 | p_paddr | Physical load address |
| 0x10 | 4 | p_filesz | Bytes to copy from file |
| 0x14 | 4 | p_memsz | Total bytes in memory (≥ filesz) |

```nasm
    mov  esi, 0x10000            ; ELF base address

    ; Verify ELF32 magic
    cmp  dword [esi], 0x464C457F ; "\x7FELF"
    jne  elf_err
    cmp  byte [esi + 4], 1      ; ELFCLASS32
    jne  elf_err
    cmp  byte [esi + 5], 1      ; little-endian
    jne  elf_err

    ; Read header fields
    mov  eax, [esi + 0x18]       ; e_entry
    mov  ebx, [esi + 0x1C]       ; e_phoff (program header offset)
    add  ebx, 0x10000            ; convert file offset to linear address
    movzx ecx, word [esi + 0x2C] ; e_phentsize (size of each PH, always 32)
    movzx edx, word [esi + 0x2E] ; e_phnum (number of program headers)
```

Loop over each program header, loading only `PT_LOAD` segments:

```nasm
.ph_loop:
    test edx, edx
    jz   .ph_done
    dec  edx

    cmp  dword [ebx], 1          ; p_type == PT_LOAD?
    jne  .ph_next

    mov  esi, [ebx + 0x04]       ; p_offset
    add  esi, 0x10000            ; source = ELF base + p_offset
    mov  edi, [ebx + 0x0C]       ; p_paddr (destination in memory)
    mov  ecx, [ebx + 0x10]       ; p_filesz (bytes to copy)

    cld
    rep  movsb                   ; copy .text, .rodata, .data

    ; Zero the BSS (p_memsz - p_filesz bytes)
    mov  ecx, [ebx + 0x14]       ; p_memsz
    sub  ecx, [ebx + 0x10]       ; p_filesz
    jz   .ph_copy_done
    mov  al, 0
    rep  stosb

.ph_copy_done:
.ph_next:
    add  ebx, ecx                ; advance to next program header entry
    jmp  .ph_loop
```

Finally, jump to the kernel entry point.  The kernel never returns:

```nasm
.ph_done:
    pop  eax                     ; e_entry
    call eax                     ; jump to kernel_main

    cli
    hlt
    jmp  $                       ; safety loop
```

**Further reading:** <https://wiki.osdev.org/ELF>

---

## 4. Long mode (IA-32e) — [TODO]

Long mode is the 64-bit operating mode of x86-64 CPUs.  To enter it from protected
mode, we need:

### Prerequisites
1. **CPUID check** — verify the CPU supports long mode (CPUID function `0x80000001`,
   EDX bit 29 = LM)
2. **PAE paging** — CR4.PAE must be set (bit 5).  Long mode requires PAE, which
   makes page-table entries 8 bytes instead of 4 and adds a 4th level (PML4)
3. **4-level paging** — PML4 → PDPT → PD → PT, identity-mapping the first 2 MiB
   (or more) using 2 MiB huge pages to keep it minimal
4. **EFER.LME** — set bit 8 of the Extended Feature Enable Register (MSR `0xC0000080`)
5. **EFER.LMA** — set automatically when paging is enabled with LME=1 (read-only)
6. **64-bit GDT** — code segment descriptor with Sz=0 (long mode), L=1

### Entry sequence
1. Disable paging (clear CR0.PG) — we need to rebuild the page tables for 64-bit
2. Enable PAE (set CR4.PAE)
3. Set EFER.LME (WRMSR to `0xC0000080`)
4. Load CR3 with 4-level PML4 address
5. Enable paging (set CR0.PG) → CPU enters compatibility mode
6. Far jump to 64-bit code segment → CPU enters long mode

### Paging in long mode
- 4 levels: PML4 → PDPT → PD → PT
- Each entry is 8 bytes (64 bits)
- Physical address space: 48-bit (256 TiB)
- Supports 4 KiB pages, 2 MiB pages, and 1 GiB pages

### Compatibility mode
After setting CR0.PG with EFER.LME=1, the CPU enters **compatibility mode** — it can
still run 32-bit protected-mode code.  The far jump to a 64-bit code segment is what
finally flips CS.L=1 and enters full 64-bit long mode.

**Further reading:** <https://wiki.osdev.org/Long_Mode>, <https://wiki.osdev.org/Setting_Up_Long_Mode>

---

## 5. Text output (VGA)

The VGA text-mode buffer lives at `0xB8000`.  Each character is 2 bytes:

| Byte | Purpose |
|------|---------|
| 0 (even) | ASCII character code |
| 1 (odd) | Attribute byte (color) |

**Attribute byte (VGA text mode):**

| Bits | Purpose |
|------|---------|
| 3:0 | Foreground color |
| 6:4 | Background color |
| 7 | Blink (or bright background, depending on mode) |

**Standard 16 VGA colors:**

| Value | Color | Value | Color |
|-------|-------|-------|-------|
| 0x0 | Black | 0x8 | Dark Gray |
| 0x1 | Blue | 0x9 | Light Blue |
| 0x2 | Green | 0xA | Light Green |
| 0x3 | Cyan | 0xB | Light Cyan |
| 0x4 | Red | 0xC | Light Red |
| 0x5 | Magenta | 0xD | Light Magenta |
| 0x6 | Brown | 0xE | Yellow |
| 0x7 | Light Gray | 0xF | White |

Example: `0x0F` = white on black.  `0x1F` = white on blue.  `0x4F` = white on red.

**Writing a character:**
```c
volatile unsigned short *vga = (unsigned short *)0xB8000;
vga[0] = 'H' | (0x0F << 8);  // 'H' in white on black at (0,0)
```

**Further reading:** <https://wiki.osdev.org/Printing_To_Screen>, <https://wiki.osdev.org/VGA_Hardware>

---

## 6. Disk layout

| LBA | Content | Size |
|---|---|---|
| 0 | Stage 1 bootsector (`boot.asm`) | 512 B |
| 1-8 | Stage 2 loader (`loader.asm`) | up to 4 KiB |
| 9+ | Kernel ELF (`kernel.elf`) | variable |

---

## 7. Memory map at kernel entry

```
0x000000 - 0x000FFF   Interrupt Vector Table (IVT)
0x007C00 - 0x007DFF   Stage 1 bootsector (dead, stack overwrites it)
0x008000 - 0x008FFF   Loader code (dead after jump)
0x009000 - 0x009FFF   Page Directory (active, 1024 PDEs)
0x00A000 - 0x00AFFF   Page Table 0 (active, maps 0-4 MiB)
0x010000 - 0x01FFFF   Kernel ELF buffer (dead, segments already copied)
0x090000               Stack top (grows down)
0x0B8000               VGA text-mode buffer (80x25)
0x100000               Kernel .text, .rodata, .data, .bss (linked here)
```
