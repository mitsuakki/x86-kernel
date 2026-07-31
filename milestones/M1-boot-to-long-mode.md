# M1 — Boot to Long Mode

**Goal:** real mode → protected mode → long mode.  Minimal paging to enable it.
Text output via VGA working.

**Status:**
- [x] Real mode bootsector (stage 1)
- [x] A20 gate, GDT, protected mode switch (stage 2)
- [x] CPUID + long mode detection (`boot/cpuid.asm`)
- [x] 64-bit GDT entry (`boot/gdt.asm`)
- [x] 4-level PAE paging, EFER.LME, long mode entry (`boot/longmode.asm`)
- [x] ELF64 parser, kernel jump (`boot/loader.asm`)
- [x] 64-bit kernel: BSS zero via linker symbols, SysV ABI `call` boundary
- [x] VGA text output (kernel.c)

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

### Loading stage 2 with CHS read

We use BIOS INT 0x13 AH=0x02 (CHS read).  Extended reads (AH=0x42) fail on floppy
drives in QEMU/SeaBIOS, so we use CHS: cylinder 0, head 0, sector 2 (LBA 1).
8 sectors = 4 KiB loaded to `0x0000:0x8000`:

```nasm
    mov ah, 0x02            ; CHS read function
    mov al, 8               ; 8 sectors (4 KiB for stage 2)
    mov ch, 0               ; cylinder 0
    mov cl, 2               ; sector 2 (LBA 1 on floppy)
    mov dh, 0               ; head 0
    mov dl, [drive_num]
    mov bx, 0x8000          ; buffer at 0x0000:0x8000
    int 0x13
    jc  disk_err

    jmp 0x0000:0x8000       ; hand off to the loader
```

If the disk read fails, we print `'1'` via BIOS teletype output and halt:

```nasm
disk_err:
    mov al, '1'
    mov ah, 0x0E            ; BIOS Teletype Output
    int 0x10
    jmp $
```

The bootsector is exactly 512 bytes, ending with the magic signature:

```nasm
times 510 - ($ - $$) db 0   ; pad with zeros to byte 510
dw 0xAA55                   ; boot signature at bytes 511-512
```

**Further reading:** <https://wiki.osdev.org/Boot_Sequence>

---

## 3. Stage 2 (`boot/loader.asm`) — the bridge

We arrive at `0x8000`, still in real mode.  The loader does five things:

1. Load the kernel ELF from disk (CHS read, 10 sectors to `0x10000`)
2. Enable the A20 gate (extracted to `boot/a20.asm`)
3. Enter protected mode (GDT at `boot/gdt.asm`; includes 64-bit code segment)
4. CPUID + long mode check (`boot/cpuid.asm`)
5. Parse the 64-bit kernel ELF, copy segments, save entry point
6. Call `enter_long_mode` (`boot/longmode.asm`) — builds 4-level PAE tables,
   enables paging, far-jumps to 64-bit long mode, zeroes BSS, calls kernel

### 3a. Load the kernel ELF from disk

```nasm
stage2_start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [drive_num], dl

    ; CHS read: 10 sectors at LBA 9 -> physical 0x10000
    mov ax, 0x1000
    mov es, ax              ; ES = 0x1000
    mov bx, 0x0000          ; ES:BX = 0x1000:0x0000 = 0x10000
    mov ah, 0x02            ; CHS read
    mov al, 10              ; 10 sectors (5120 bytes)
    mov ch, 0               ; cylinder 0
    mov cl, 10              ; sector 10 (LBA 9 -> (9%18)+1)
    mov dh, 0               ; head 0
    mov dl, [drive_num]
    int 0x13
    jc  disk_err
```

### 3b. Enable the A20 gate (`boot/a20.asm`)

Extracted to its own file for modularity.  Full fallback chain: check-first → BIOS
INT 0x15 (AX=0x2401) → keyboard controller (8042, ports 0x64/0x60) → Fast A20 Gate
(port 0x92).

The check uses the wraparound trick: write 0x00 to `0x0000:0x0500` and 0xFF to
`0xFFFF:0x0510`.  If A20 is off, both writes hit the same physical byte (bit 20
masked to 0).  If they read back different, A20 is on.

See `boot/a20.asm` for the full implementation (~125 lines).

**Further reading:** <https://wiki.osdev.org/A20_Line>

### 3c. Enter protected mode (`boot/gdt.asm`)

The GDT defines four descriptors in a flat memory model (base=0, limit=4 GiB):

| Selector | Name | Sz | L | Purpose |
|----------|------|----|---|---------|
| 0x00 | Null | — | — | Required by CPU |
| 0x08 | Code32 | 1 | 0 | 32-bit protected mode |
| 0x10 | Data | 1 | 0 | Data (works in 32-bit and long mode) |
| 0x18 | Code64 | 0 | 1 | 64-bit long mode |

The 64-bit code segment has Sz=0, L=1.  A 4 GiB limit is required — the CPU checks
it on the far jump before ignoring limits in long mode.

```nasm
    cli
    lgdt [gdt_descriptor]

    mov  eax, cr0
    or   eax, 1                 ; set PE (Protection Enable)
    mov  cr0, eax

    jmp  0x08:pmode_entry       ; far jump: loads CS with 32-bit code selector
```

After the far jump, in 32-bit protected mode:

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

### 3d. CPUID + long mode check (`boot/cpuid.asm`)

Two-stage check:

1. **CPUID availability** (`check_cpuid`): toggle EFLAGS bit 21.  On 486+ with
   CPUID, the bit toggles.  On 386 it's always 1; on 8086/286 it's always 0.

2. **Long mode support** (`check_long_mode`): first query CPUID 0x80000000 to
   verify extended functions exist (eax >= 0x80000001), then CPUID 0x80000001
   to test EDX bit 29 (LM — Long Mode).

```nasm
pmode_entry:
    ; ... segment setup ...

    call check_long_mode        ; returns eax=1 if supported
    test eax, eax
    jz   .no_long_mode          ; halt with 'C' on VGA
```

### 3e. Parse the 64-bit kernel ELF

The kernel is a 64-bit ELF at `0x10000`.  ELF64 header offsets differ from ELF32:

**ELF64 header layout** (key fields):
| Offset | Size | Name | Meaning |
|--------|------|------|---------|
| 0x00 | 4 | e_ident[0:4] | Magic: `\x7FELF` |
| 0x04 | 1 | e_ident[4] | Class: 2 = 64-bit |
| 0x05 | 1 | e_ident[5] | Data: 1 = little-endian |
| 0x18 | 8 | e_entry | Entry point virtual address |
| 0x20 | 8 | e_phoff | Program header table offset |
| 0x36 | 2 | e_phentsize | Size of each program header |
| 0x38 | 2 | e_phnum | Number of program headers |

**ELF64 program header layout** (56 bytes each):
| Offset | Size | Name | Meaning |
|--------|------|------|---------|
| 0x00 | 4 | p_type | 1 = PT_LOAD |
| 0x08 | 8 | p_offset | Offset in file |
| 0x18 | 8 | p_paddr | Physical load address |
| 0x20 | 8 | p_filesz | Bytes to copy from file |
| 0x28 | 8 | p_memsz | Total bytes in memory (≥ filesz) |

All 8-byte fields: we read the low 32 bits (kernel is linked at 1 MiB, fits in
32 bits).  For each PT_LOAD segment: copy from ELF buffer to p_paddr, save BSS
bounds (p_paddr + p_filesz → `__bss_start`, p_paddr + p_memsz → `__bss_end`),
and save the 64-bit entry point to `kernel_entry` (dq 0, 8 bytes, in
`boot/longmode.asm`).

### 3f. Enter long mode (`boot/longmode.asm`)

`enter_long_mode` (32-bit code, called from pmode_entry):

1. Disable 32-bit paging (clear CR0.PG) — safe, we're identity-mapped
2. Clear and build 4-level PAE tables at `0x9000`–`0xBFFF`:
   - PML4[0] at `0x9000` → PDPT at `0xA000` (Present | Writable)
   - PDPT[0] at `0xA000` → PD at `0xB000`
   - PD[0] at `0xB000` → 2 MiB huge page at physical 0x0 (PS=1)
   - Identity-maps first 2 MiB
3. Load CR3 with PML4 address (`0x9000`)
4. Enable CR4.PAE (bit 5)
5. Set EFER.LME (MSR `0xC0000080`, bit 8) via RDMSR/WRMSR
6. Enable CR0.PG → CPU enters **compatibility mode**
7. Far jump `GDT_CODE64:long_mode_entry` → CPU enters **64-bit long mode**

`long_mode_entry` (64-bit code):

1. Load data segments (DS, ES, FS, GS, SS = `0x10`)
2. Zero kernel BSS: load `__bss_start` and `__bss_end` (saved by ELF64 parser),
   compute count, `rep stosb`
3. Align RSP to 16 bytes (`and rsp, ~0xF`) — SysV AMD64 ABI requires
   `RSP % 16 == 0` before `call`
4. Load 64-bit kernel entry from `kernel_entry`, `call rax` → kernel

**Paging structure (4-level PAE):**
| Level | Address | Entries | Entry size | Maps |
|-------|---------|---------|------------|------|
| PML4 | 0x9000 | 1 used / 512 | 8 bytes | → PDPT at 0xA000 |
| PDPT | 0xA000 | 1 used / 512 | 8 bytes | → PD at 0xB000 |
| PD (huge) | 0xB000 | 1 used / 512 | 8 bytes | 2 MiB @ phys 0, PS=1 |

**Key registers after entry:**
| Register | Value | Meaning |
|----------|-------|---------|
| EFER.LMA | 1 | Long Mode Active |
| CS.L | 1 | 64-bit code segment |
| CR0.PG | 1 | Paging enabled |
| CR4.PAE | 1 | PAE active |
| CR3 | 0x9000 | PML4 base |

**Further reading:** <https://wiki.osdev.org/Long_Mode>, <https://wiki.osdev.org/Setting_Up_Long_Mode>

---

## 4. Kernel (`kernel/kernel.c` + `kernel/linker.ld`)

The kernel is compiled as 64-bit (`-m64`, `-mcmodel=large`, `-mno-red-zone`) and
linked at `0x100000` (1 MiB).  The linker script exports `__bss_start` and
`__bss_end` for the loader's BSS zeroing.

Entry point `kernel_main`:
1. Calls `clear_screen()` — fills VGA buffer with spaces
2. Calls `write("Test", VGA_COLOR_WHITE)` — puts characters on screen
3. Infinite loop `for (;;) {}`

The transition from assembly to C follows SysV AMD64 ABI: stack 16-byte aligned,
entry called via `call` (not `jmp`), 64-bit registers.

**Further reading:** <https://wiki.osdev.org/Printing_To_Screen>, <https://wiki.osdev.org/VGA_Hardware>

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

**Further reading:** <https://wiki.osdev.org/Printing_To_Screen>, <https://wiki.osdev.org/VGA_Hardware>

---

## 6. Disk layout

| LBA | Content | Size |
|---|---|---|
| 0 | Stage 1 bootsector (`boot.asm`) | 512 B |
| 1-8 | Stage 2 loader (`loader.asm`) + includes | up to 4 KiB |
| 9+ | Kernel ELF (`kernel.elf`, 64-bit) | variable (~5 KiB) |

---

## 7. Memory map at kernel entry

```
0x000000 - 0x000FFF   Interrupt Vector Table (IVT)
0x007C00 - 0x007DFF   Stage 1 bootsector (dead, stack overwrites it)
0x008000 - 0x008FFF   Loader code (dead after jump)
0x009000 - 0x009FFF   PML4 table (active, 1 entry used)
0x00A000 - 0x00AFFF   PDPT table (active, 1 entry used)
0x00B000 - 0x00BFFF   PD table (active, 1 entry used — 2 MiB huge page)
0x010000 - 0x01FFFF   Kernel ELF buffer (dead, segments already copied)
0x090000               Stack top (grows down, 16-byte aligned at call)
0x0B8000               VGA text-mode buffer (80x25)
0x100000               Kernel .text, .rodata, .data, .bss (linked here)
```

---

## 8. Source files

| File | Purpose | BITS |
|------|---------|------|
| `boot/boot.asm` | Stage 1 bootsector (CHS load, 512 B) | 16 |
| `boot/loader.asm` | Stage 2: CHS load, A20, GDT, CPUID, ELF64 parse | 16→32 |
| `boot/a20.asm` | A20 check + enable (BIOS, 8042, Fast A20) | 16 |
| `boot/gdt.asm` | GDT: null, code32, data, code64 | 16 |
| `boot/cpuid.asm` | CPUID check + long mode detection | 32 |
| `boot/longmode.asm` | 4-level PAE tables, enter long mode, BSS zero, call kernel | 32→64 |
| `kernel/kernel.c` | C entry: clear_screen, write, loop | (C, -m64) |
| `kernel/linker.ld` | Link at 0x100000, export __bss_start/__bss_end | — |
| `Makefile` | Build: nasm, gcc -m64, ld -melf_x86_64, dd image | — |
