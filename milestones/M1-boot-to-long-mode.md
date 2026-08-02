# M1 Boot to Long Mode

**Goal:** real mode → protected mode → long mode. Minimal paging to enable it.
Text output via VGA working.

**Status:**
- [x] Real mode bootsector (stage 1)
- [x] A20 gate, GDT, protected mode switch (stage 2)
- [x] CPUID + long mode detection (`boot/stage2/cpuid.asm`)
- [x] 64-bit GDT entry (`boot/stage2/gdt.asm`)
- [x] 4-level PAE paging, EFER.LME, long mode entry (`boot/stage2/longmode.asm`)
- [x] ELF64 parser, kernel jump (`boot/stage2/loader.asm`)
- [x] 64-bit kernel: BSS zero via linker symbols, SysV ABI `call` boundary
- [x] VGA text output (`kernel.c`)

---

## 1. The boot process: BIOS to our code

### 1a. What happens when the power button is pressed

When the PC powers on, the CPU is hardwired to start executing at physical
address `0xFFFFFFF0`, a location in the motherboard firmware ROM. This firmware,
called the BIOS (Basic Input/Output System), runs a Power-On Self Test (POST) to
verify that memory, the keyboard, and basic peripherals are present and
functional.

After POST, the BIOS searches for a bootable disk. It iterates through a
configured boot order (floppy, hard disk, CD-ROM, USB), reads the first sector
(512 bytes) of each candidate into physical memory at address `0x7C00`, and
checks whether the sector ends with the magic bytes `0x55 0xAA`. If the bytes
match, the BIOS loads the full 512-byte sector to `0x7C00` and jumps to it. If
they do not match, the BIOS moves to the next device.

At this moment, the CPU is in **real mode**, the operational state inherited from
the original 8086 processor released in 1978.

**Why `0x7C00`?** The address comes from the IBM PC 5150 (1981). The BIOS and
BASIC interpreter in ROM occupied the top of the 1 MiB address space, and the
Interrupt Vector Table (IVT) occupied the bottom 1 KiB. `0x7C00` was chosen as a
compromise: it leaves 512 bytes of stack space below it (the stack grows down)
and roughly 31 KiB of free space above it for the booted code.

### 1b. Real mode: the constraints we start with

Real mode is a 16-bit execution environment with no memory protection, no
privilege levels, and no paging. Every byte of physical memory is accessible to
every instruction. The CPU operates as though it were an 8086, with a few
extensions borrowed from the 80286 and 80386.

The defining feature of real mode is **segmented addressing**. The CPU has four
segment registers (`CS`, `DS`, `ES`, `SS`) that hold 16-bit values. To compute a
physical address, the CPU shifts the segment value left by 4 bits (multiply by
16) and adds a 16-bit offset:

```
Physical address = segment × 16 + offset
```

For example, `0x07C0:0x0000` and `0x0000:0x7C00` both resolve to physical
address `0x7C00`. A given physical byte can be addressed by 4096 different
segment:offset combinations. This is not a feature: it is an artifact of the
8086's 20-bit address bus squeezed through 16-bit registers.

The practical limits of real mode:

| Constraint | Value | Why |
|------------|-------|-----|
| Address bus | 20 bits (1 MiB) | 8086 had 20 address pins |
| Instruction size | 16-bit | Default operand and address size |
| Register width | 16-bit | AX, BX, CX, DX, etc. |
| Memory protection | None | No privilege rings, no page tables |
| Interrupt dispatch | IVT at `0x0000` | 256 × 4-byte far pointers |

**Further reading:** <https://wiki.osdev.org/Real_Mode>,
<https://wiki.osdev.org/Boot_Sequence>

---

## 2. Stage 1 the bootsector (`boot/stage1/boot.asm`)

### 2a. What the bootsector must accomplish

The BIOS gives us 512 bytes at `0x7C00` and executes it. The bootsector's job
is narrow: load the next stage (the loader) from disk into memory, then jump to
it. We cannot load the kernel directly from stage 1 because the bootsector's
512-byte budget is too tight for ELF parsing, CPU feature detection, and the
protected-mode switch. Those are stage 2's responsibilities.

### 2b. Segment register initialization

The BIOS makes no guarantee about the values of segment registers. `CS` is
typically `0x0000` and `IP` is `0x7C00`, but `DS`, `ES`, and `SS` could contain
anything left over from the BIOS initialization sequence. We zero them
explicitly and set up a known stack:

```nasm
[ORG 0x7C00]
[BITS 16]

start:
  xor ax, ax
  mov ds, ax
  mov es, ax
  mov ss, ax
  mov sp, 0x7C00     ; stack grows down from our code
  mov [drive_num], dl   ; save BIOS drive number
```

`[ORG 0x7C00]` tells NASM that the code will be loaded at offset `0x7C00` within
the code segment. Without this directive, NASM would compute label addresses
starting from 0, and `jmp` instructions would target wrong locations.

The stack pointer is set to `0x7C00`. Since the stack grows **downward** (SP
decrements on `push`), the stack occupies the region below `0x7C00`, safely away
from our code which occupies `0x7C00`–`0x7DFF`.

The BIOS passes the boot drive number in `DL` (e.g., `0x00` for floppy, `0x80`
for the first hard disk). We save it because we need to pass it back to the BIOS
when reading additional sectors.

### 2c. Reading stage 2 from disk

To load the loader, we must ask the BIOS to read sectors from the boot disk into
memory. The BIOS provides two disk read interfaces:

| Interface | INT | Addressing | Max sectors | Notes |
|-----------|-----|------------|-------------|-------|
| CHS read | `0x13 AH=0x02` | Cylinder/Head/Sector | 255 per call | Works everywhere, including floppy emulation |
| Extended read | `0x13 AH=0x42` | Linear LBA (Logical Block Address) | 127 per call | Requires EDD BIOS (not available on all floppy controllers) |

We use CHS (Cylinder-Head-Sector) because it works on QEMU's floppy disk
emulation, where extended reads sometimes fail. The conversion from LBA to CHS
for a floppy disk (2 heads, 18 sectors per track, 80 cylinders):

- LBA 1 → cylinder 0, head 0, sector 2 (LBA 0 is the bootsector itself)
- 8 sectors starting at LBA 1 give us 4 KiB of loader code

```nasm
  mov ah, 0x02      ; CHS read function
  mov al, 8        ; 8 sectors (4 KiB for stage 2)
  mov ch, 0        ; cylinder 0
  mov cl, 2        ; sector 2 (LBA 1 on floppy)
  mov dh, 0        ; head 0
  mov dl, [drive_num]   ; boot drive
  mov bx, 0x8000     ; buffer at ES:BX = 0x0000:0x8000
  int 0x13
  jc disk_err
```

Each parameter explained:

| Parameter | Register | Meaning |
|-----------|----------|---------|
| Function | `AH = 0x02` | Read sectors into memory |
| Count | `AL = 8` | Number of sectors to read |
| Cylinder | `CH = 0` | Cylinder number (0-indexed, 0–79 for floppy) |
| Sector | `CL = 2` | Starting sector (1-indexed, 1–18 for floppy) |
| Head | `DH = 0` | Head number (0 or 1 for floppy) |
| Drive | `DL` | Drive number (0x00 = floppy, 0x80 = first HDD) |
| Buffer | `ES:BX = 0x0000:0x8000` | Destination in memory |

After the call, the BIOS sets the Carry Flag (`CF`) on error. If the read fails
(disk missing, bad sector, drive not ready), we print `'1'` to the screen via
BIOS teletype output and halt:

```nasm
disk_err:
  mov al, '1'
  mov ah, 0x0E      ; BIOS Teletype Output
  int 0x10
  jmp $          ; infinite loop = halt
```

### 2d. Handoff to stage 2

```nasm
  jmp 0x0000:0x8000
```

A far jump sets both `CS` and `IP`. We set `CS = 0x0000` and `IP = 0x8000` so
that the loader's `[ORG 0x8000]` matches reality. If we used a near `jmp`, only
`IP` would change, and `CS` might retain whatever value the BIOS left in it.

### 2e. The boot signature

```nasm
times 510 - ($ - $$) db 0  ; pad with zeros to byte 510
dw 0xAA55          ; boot signature at bytes 511-512
```

`$` is the current assembly position, `$$` is the start of the section. `times
510 - ($ - $$) db 0` fills the gap between the end of our code and byte 510 with
zeros. The `dw 0xAA55` writes the magic signature. Without these two bytes at
these exact offsets, the BIOS will not recognize the sector as bootable and will
skip to the next device.

The bytes are written as `0x55 0xAA` in memory (x86 is little-endian: the least
significant byte comes first). This is why many references say "ends with `0x55
0xAA`" despite the code writing `0xAA55`.

**Source file:** `boot/stage1/boot.asm`

**Further reading:** <https://wiki.osdev.org/Boot_Sequence>,
<https://wiki.osdev.org/MBR_(x86)>

---

## 3. Stage 2 the loader (`boot/stage2/loader.asm`)

### 3a. Overview

The loader arrives at `0x8000` in real mode. It has six responsibilities:

1. Load the kernel ELF from disk (CHS read, 64 sectors / 32 KiB to `0x10000`)
2. Enable the A20 gate (extracted to `boot/stage2/a20.asm`)
3. Switch to protected mode (GDT at `boot/stage2/gdt.asm`; includes 64-bit code segment)
4. CPUID + long mode check (`boot/stage2/cpuid.asm`)
5. Parse the 64-bit kernel ELF: copy loadable segments, save BSS bounds, record entry point
6. Call `enter_long_mode` (`boot/stage2/longmode.asm`) builds 4-level PAE page tables, enables paging, far-jumps to 64-bit long mode, zeroes BSS, calls kernel

This is the last code that can use BIOS services. Once we enter protected mode,
the real-mode IVT at `0x0000` becomes meaningless and `INT` instructions trigger
a `#GP` fault instead of calling BIOS routines.

### 3b. Loading the kernel ELF from disk

The kernel ELF is at LBA 9 (immediately after the 8-sector loader). We load
64 sectors (32 KiB) to physical address `0x10000`, well above the loader and
page tables:

```nasm
stage2_start:
  xor ax, ax
  mov ds, ax
  mov es, ax
  mov ss, ax
  mov sp, 0x7C00

  mov [drive_num], dl

  mov ax, 0x1000
  mov es, ax       ; ES = 0x1000
  mov bx, 0x0000     ; ES:BX = 0x1000:0x0000 = 0x10000
  mov ah, 0x02      ; CHS read
  mov al, 64       ; 64 sectors (32 KiB)
  mov ch, 0        ; cylinder 0
  mov cl, 10       ; sector 10 (LBA 9)
  mov dh, 0        ; head 0
  mov dl, [drive_num]
  int 0x13
  jc disk_err
```

The buffer at `0x10000` (1 MiB) is chosen because it is above the memory we use
for page tables (`0x9000`–`0xBFFF`), the loader itself (`0x8000`), and the BDA
(BIOS Data Area, `0x0400`–`0x04FF`). After the ELF segments are copied to their
link addresses, the `0x10000` buffer becomes dead memory and can be reused.

32 KiB is enough for the kernel at this milestone. The kernel ELF contains `.text`
(code), `.rodata` (read-only data like exception name strings), `.data`
(initialized globals), and `.bss` (zero-initialized globals, which occupy no
space in the ELF file).

**Source file:** `boot/stage2/loader.asm`

---

## 4. The A20 gate (`boot/stage2/a20.asm`)

### 4a. The problem: address wraparound

The 8086 had a 20-bit address bus and 16-bit registers. To access addresses
above 1 MiB, software used a segment base of `0xFFFF` and an offset greater than
`0x000F`. For example, `0xFFFF:0x0010` resolves to:

```
0xFFFF × 16 + 0x0010 = 0xFFFF0 + 0x0010 = 0x100000
```

This is a 21-bit result. The 8086, having only 20 address pins (A0–A19), simply
dropped the 21st bit and wrapped the address to `0x000000`. Software on the
original IBM PC relied on this wraparound behavior: programs accessed the bottom
of memory by addressing above 1 MiB.

The 80286 introduced a 24-bit address bus (16 MiB addressable). Suddenly,
`0xFFFF:0x0010` reached real physical memory at `0x100000`. Old programs that
expected wraparound broke. To maintain backward compatibility, IBM added a
**gate** (a latch on the A20 line of the address bus) that could be forced low
(masking bit 20 to 0, emulating 8086 wraparound) or allowed to pass through
naturally (enabling access to memory above 1 MiB). This is the A20 gate.

When the PC boots, the BIOS leaves the A20 gate **disabled** (A20 forced to 0)
for backward compatibility. Before we can use memory above 1 MiB (where our
kernel lives at `0x100000`), we must enable it.

### 4b. The check: is A20 already on?

The check exploits the wraparound itself. We compare two addresses that map to
the same physical byte when A20 is off, and to different bytes when A20 is on:

```
Address 1: 0x0000:0x0500 → physical 0x000500
Address 2: 0xFFFF:0x0510 → physical 0x100500 (A20 on) or 0x000500 (A20 off)
```

The two segment:offset pairs are chosen so that their lower 20 bits are
identical. The calculation:

- `0x0000 × 16 + 0x0500 = 0x000500`
- `0xFFFF × 16 + 0x0510 = 0xFFFF0 + 0x0510 = 0x100500`

If bits 20 and above are masked, both resolve to `0x000500`.

```nasm
check_a20:
  push ds
  push es

  xor ax, ax
  mov ds, ax         ; ds = 0x0000
  not ax
  mov es, ax         ; es = 0xFFFF

  ; Save original bytes at both locations
  mov al, [ds:0x0500]
  mov ah, [es:0x0510]
  push ax

  ; Write different values
  mov byte [ds:0x0500], 0x00
  mov byte [es:0x0510], 0xFF

  ; Read back: if they match, wraparound occurred → A20 is off
  cmp byte [ds:0x0500], 0xFF
  mov ax, 1
  jne .enabled         ; different → A20 on
  xor ax, ax          ; same → A20 off

.enabled:
  ; Restore original bytes (we don't own this memory)
  pop ax
  mov [ds:0x0500], al
  mov [es:0x0510], ah

  pop es
  pop ds
  ret
```

We save and restore the original bytes at both addresses because the memory
locations `0x0500` and `0x0510` might contain BIOS data or other boot-state
information. A well-behaved bootloader leaves memory as it found it.

### 4c. The three fallback methods

Three methods exist to enable A20, each with different hardware compatibility:

**Method 1 BIOS interrupt `0x15 AX=0x2401`:**

```nasm
  mov ax, 0x2401
  int 0x15
```

The BIOS provides a dedicated A20 service. This is the safest method and works
on most systems, but a few BIOS implementations ignore it. We try it first.

**Method 2 Keyboard controller (8042/8742):**

The 8042 keyboard controller has a spare pin wired to the A20 gate. We send a
command sequence: disable the keyboard, read the controller output port, set bit
1 (the A20 bit), write it back, re-enable the keyboard. Each step requires
polling the controller's status register (port `0x64`) to ensure it is ready:

- Bit 1 of the status register = 1: input buffer full, wait before writing
- Bit 0 of the status register = 1: output buffer full, data is ready to read

```nasm
.wait_in:            ; wait until input buffer empty (bit 1 = 0)
  in  al, 0x64
  test al, 2
  jnz .wait_in
  ret

.wait_out:           ; wait until output buffer full (bit 0 = 1)
  in  al, 0x64
  test al, 1
  jz  .wait_out
  ret
```

This method works on nearly all real hardware but adds noticeable latency (the
8042 runs at roughly 8 MHz with slow response times).

**Method 3 Fast A20 Gate (port `0x92`):**

```nasm
  in  al, 0x92
  test al, 2
  jnz .fast_done       ; bit 1 already set
  or  al, 2
  and al, 0xFE        ; keep bit 0 clear (prevent fast reset)
  out 0x92, al
```

Port `0x92` is the System Control Port A on the PS/2 chipset. Bit 1 controls
the A20 gate directly, with no keyboard controller involvement. We read the
current value, set bit 1, and write it back. We explicitly clear bit 0 because
setting it triggers a fast CPU reset (the equivalent of pressing Ctrl+Alt+Del).

This method is the simplest and fastest, but it exists only on PS/2-compatible
systems. Older hardware (pre-PS/2) does not have port `0x92`.

The full chain: check first → BIOS `0x15` → keyboard 8042 → Fast A20. After
each method, we re-check whether A20 is actually enabled. If all three methods
fail, we continue anyway because QEMU and most virtual machines already have A20
enabled at boot.

**Source file:** `boot/stage2/a20.asm`

**Further reading:** <https://wiki.osdev.org/A20_Line>

---

## 5. The Global Descriptor Table GDT (`boot/stage2/gdt.asm`)

### 5a. What the GDT does

In protected mode and long mode, memory access is no longer governed by
`segment × 16 + offset`. Instead, every segment register holds a **selector**,
which is an index into the GDT. The GDT entry (called a **descriptor**)
specifies the base address, the size limit, and the access permissions for that
segment. The CPU consults the GDT on every memory access.

A flat memory model sets all segment descriptors to base = 0 and limit = 4 GiB,
making every segment cover the entire address space. This is the simplest model
for a kernel: addresses are not transformed by the segment, so the virtual
address equals the physical address (identity mapping, from the segmentation
perspective).

### 5b. Descriptor layout

Each GDT descriptor is exactly 8 bytes (64 bits). The layout is, charitably,
a historical accident: the 80286 (1982) defined the first 6 bytes, the 80386
(1985) added the base and limit extensions, and x86-64 (2003) repurposed the
`Sz` and `L` bits in the flags field.

| Bits | Size | Field | Meaning |
|------|------|-------|---------|
| 0–15 | 2 B | Limit[15:0] | Low 16 bits of segment limit |
| 16–31 | 2 B | Base[15:0] | Low 16 bits of segment base address |
| 32–39 | 1 B | Base[23:16] | Middle 8 bits of base |
| 40–47 | 1 B | Access byte | Type, DPL, Present (see below) |
| 48–51 | 4 b | Limit[19:16] | High 4 bits of limit |
| 52–55 | 4 b | Flags | Granularity, Size, Long mode, Reserved |
| 56–63 | 1 B | Base[31:24] | High 8 bits of base |

### 5c. The access byte (bits 40–47), bit by bit

The access byte controls what the segment is allowed to do:

| Bit | Name | Meaning |
|-----|------|---------|
| 7 | Present (P) | 1 = descriptor is valid. The CPU `#NP`-faults on loads to non-present segments. |
| 6–5 | DPL (Descriptor Privilege Level) | 0 = ring 0 (kernel), 3 = ring 3 (user). Controls who may load this selector. |
| 4 | S (System/Descriptor type) | 1 = code/data segment, 0 = system descriptor (TSS, LDT, call gate). |
| 3 | E (Executable) | 1 = code segment (can be jumped to), 0 = data segment (can be read/written). |
| 2 | DC (Direction/Conforming) | Code: 1 = conforming (can be called from lower DPL), 0 = non-conforming. Data: 1 = expand-down, 0 = expand-up. |
| 1 | RW (Readable/Writable) | Code: 1 = readable (can `mov` from CS), 0 = execute-only. Data: 1 = writable, 0 = read-only. |
| 0 | A (Accessed) | Set to 1 by the CPU the first time the selector is loaded. Software can use this for swapping heuristics. |

### 5d. The flags nibble (bits 52–55), bit by bit

| Bit | Name | Meaning |
|-----|------|---------|
| 55 | Gr (Granularity) | 0 = limit in bytes, 1 = limit in 4 KiB pages. With Gr=1 and limit=0xFFFFF, the effective limit is 0xFFFFFFFF (4 GiB). |
| 54 | Sz (Size) | 0 = 16-bit protected mode, 1 = 32-bit protected mode. Ignored when L=1 (64-bit overrides both). |
| 53 | L (Long mode) | 1 = 64-bit code segment. Must be 0 for data segments. |
| 52 | Reserved | Must be 0. |

When L=1, the CPU ignores Sz (and the segment limit, for that matter in long
mode, segmentation is effectively disabled, and all segments cover the entire
64-bit virtual address space). The limit must still be set to a valid value,
however, because the CPU checks it during the far jump before entering long mode
and after entering long mode, and a malformed limit causes a `#GP` fault.

### 5e. Our GDT: four descriptors in a flat model

| Selector | Label | Sz | L | Access byte | Purpose |
|----------|-------|----|---|-------------|---------|
| `0x00` | Null | | | `0x00` | Required by CPU. Any load of selector 0 triggers `#GP`. Catches null-pointer bugs at the hardware level. |
| `0x08` | Code32 | 1 | 0 | `0x9A` (Present, DPL 0, code, non-conforming, readable) | 32-bit protected mode. We use this for the `jmp` from real mode into protected mode, and for the ELF64 parser + paging setup code. |
| `0x10` | Data | 1 | 0 | `0x92` (Present, DPL 0, data, expand-up, writable) | Data segments (DS, ES, FS, GS, SS). Works in both 32-bit protected mode and 64-bit long mode. The CPU ignores the Sz bit for data segments in long mode; only the L bit matters for code. |
| `0x18` | Code64 | 0 | 1 | `0x9A` (Present, DPL 0, code, non-conforming, readable) | 64-bit long mode. L=1, Sz=0 (ignored). Limit is 0xFFFFF with Gr=1 = 4 GiB effective. |

The access byte `0x9A` for code segments is `0b10011010`:
- Bit 7 (P) = 1: present
- Bits 6:5 (DPL) = 00: ring 0
- Bit 4 (S) = 1: code/data segment
- Bit 3 (E) = 1: executable (code)
- Bit 2 (DC) = 0: non-conforming
- Bit 1 (RW) = 1: readable
- Bit 0 (A) = 0: not yet accessed (CPU sets this)

The access byte `0x92` for the data segment is `0b10010010`:
- Bit 3 (E) = 0: not executable (data)
- Bit 2 (DC) = 0: expand-up
- Bit 1 (RW) = 1: writable

### 5f. Loading the GDT and entering protected mode

The CPU needs to know the address and size of the GDT. This is provided by the
GDTR (Global Descriptor Table Register), loaded via the `lgdt` instruction:

```nasm
gdt_descriptor:
  dw gdt_end - gdt_start - 1  ; limit = size of GDT minus 1
  dd gdt_start         ; linear address of the GDT
```

The limit is the total byte size minus 1 (the CPU treats it like a maximum
offset). A smaller limit than expected means the last partially-accessible
descriptor triggers a `#GP` on access, acting as a bounds check.

Protected mode is enabled by setting bit 0 (PE, Protection Enable) in control
register CR0:

```nasm
  cli             ; disable interrupts (real-mode IVT becomes invalid)
  lgdt [gdt_descriptor]

  mov eax, cr0
  or  eax, 1         ; set PE (Protection Enable)
  mov cr0, eax

  jmp 0x08:pmode_entry    ; far jump: loads CS with 32-bit code selector
```

The `cli` before `lgdt` is critical. Real-mode interrupts use the IVT at
`0x0000`, but after entering protected mode, the CPU expects an IDT (Interrupt
Descriptor Table). If a hardware interrupt fires before we set up the IDT, the
CPU will interpret the real-mode IVT entries as protected-mode IDT entries and
jump to a garbage address. Disabling interrupts prevents this race.

The far jump sets `CS` to the Code32 selector (`0x08`). The CPU internally
caches the descriptor's base, limit, and access rights from the GDT into hidden
parts of the CS register. A near jump is insufficient: it would not reload `CS`,
and the CPU would still be using the real-mode segment cache.

After the far jump, in 32-bit protected mode, we load the data segment registers:

```nasm
[BITS 32]
pmode_entry:
  mov ax, 0x10        ; data segment selector
  mov ds, ax
  mov es, ax
  mov fs, ax
  mov gs, ax
  mov ss, ax
  mov esp, 0x90000      ; stack at ~576 KiB
```

The stack is moved to `0x90000`, well above the loader (`0x8000`), the page
tables we are about to build (`0x9000`–`0xBFFF`), and the kernel buffer
(`0x10000`). The stack grows down from `0x90000`, giving us roughly 512 KiB of
stack space before colliding with the kernel buffer.

**Source file:** `boot/stage2/gdt.asm`

**Further reading:** <https://wiki.osdev.org/Protected_Mode>,
<https://wiki.osdev.org/GDT>, <https://wiki.osdev.org/GDT_Tutorial>

---

## 6. CPUID and long mode detection (`boot/stage2/cpuid.asm`)

### 6a. The two questions we must answer

Before we can enter long mode, we must verify two things:

1. Does this CPU support the `CPUID` instruction? (Without it, we cannot query.)
2. Does this CPU support long mode (x86-64)?

A CPU without long mode support (pre-2003 32-bit-only processor) cannot execute
our 64-bit kernel. We must detect this and halt with an error message rather
than crash mysteriously.

### 6b. CPUID detection: the EFLAGS bit 21 trick

The `CPUID` instruction was introduced with the 80486 in 1989. CPUs before the
486 (8086, 286, 386) do not have it. There is no "I am a 486+" register to
check. Instead, we use a side effect: bit 21 of EFLAGS (the ID flag) can be
toggled by software **only if** the CPU supports `CPUID`.

| Platform | EFLAGS bit 21 behavior |
|----------|----------------------|
| 8086/8088 | No EFLAGS register at all (no `pushfd`/`popfd`; different detection needed) |
| 80286 | Bit 21 is always 0, cannot be set |
| 80386 | Bit 21 is always 1, cannot be cleared |
| 80486+ with CPUID | Bit 21 toggles freely |

The algorithm:

```nasm
check_cpuid:
  pushfd             ; read EFLAGS
  pop eax

  mov ecx, eax          ; save original
  xor eax, (1 << 21)       ; flip bit 21

  push eax            ; attempt to write modified EFLAGS
  popfd

  pushfd             ; read back
  pop eax

  push ecx            ; restore original EFLAGS
  popfd

  xor eax, ecx          ; did the bit actually change?
  jnz .supports_id        ; non-zero → yes, CPUID supported
  xor eax, eax          ; zero → not supported
  ret

.supports_id:
  mov ax, 1
  ret
```

If `eax ^ ecx != 0`, the write succeeded, meaning the CPU supports CPUID.
Otherwise, either the bit was stuck at 0 (286) or stuck at 1 (386), and we
cannot proceed.

### 6c. Long mode detection: extended CPUID functions

CPUID is an instruction that takes a function number in `EAX` and returns
feature information in `EAX`, `EBX`, `ECX`, and `EDX`. Standard functions are
`0x00000000`–`0x0000000F`; extended functions start at `0x80000000`.

The detection sequence:

1. Call CPUID with `EAX = 0x80000000`. The CPU returns the maximum supported
  extended function number in `EAX`.
2. If `EAX < 0x80000001`, the CPU does not expose the extended feature flags
  function. Long mode cannot be verified.
3. Call CPUID with `EAX = 0x80000001`. The CPU returns feature flags in `EDX`.
  Test bit 29 (LM, Long Mode):

```nasm
check_long_mode:
  call check_cpuid
  test eax, eax
  jz .no_lm

  mov eax, 0x80000000
  cpuid
  cmp eax, 0x80000001
  jb .no_lm

  mov eax, 0x80000001
  cpuid
  test edx, (1 << 29)
  jz .no_lm

  mov eax, 1
  ret

.no_lm:
  xor eax, eax
  ret
```

In `loader.asm`, we call this function and check the result. If long mode is not
supported, we write `'C'` to the top-left corner of the VGA buffer and halt:

```nasm
  call check_long_mode
  test eax, eax
  jz  .no_long_mode

  ; ... proceed to ELF parsing and long mode entry ...

.no_long_mode:
  mov byte [0xB8000], 'C'    ; red-on-black 'C' = CPU error
  mov byte [0xB8001], 0x4F
  cli
  hlt
  jmp $
```

**Source file:** `boot/stage2/cpuid.asm`

**Further reading:** <https://wiki.osdev.org/CPUID>,
<https://wiki.osdev.org/Setting_Up_Long_Mode>

---

## 7. The ELF64 parser (`boot/stage2/loader.asm`, 32-bit section)

### 7a. What the parser must extract

The kernel is a 64-bit ELF binary loaded at `0x10000`. The ELF format is a
standard executable and linkable format used by Linux, FreeBSD, and virtually all
Unix-like systems. An ELF file contains:

1. An **ELF header** describing the file structure and target architecture.
2. A **program header table** listing the segments to be loaded into memory.
3. **Sections** (`.text`, `.rodata`, `.data`, `.bss`) whose data is embedded in
  the segments.

For the bootloader, we only care about the program headers. Each `PT_LOAD`
header describes a contiguous block of memory to be filled:

- `p_offset`: where the data is in the ELF file
- `p_paddr`: where it should go in physical memory
- `p_filesz`: how many bytes to copy from the file
- `p_memsz`: total size in memory (the difference `memsz - filesz` is zeroed as BSS)

### 7b. ELF64 header: the fields we read

The ELF64 header is 64 bytes long. The fields relevant to the bootloader are:

| Offset | Size | Field | Use |
|--------|------|-------|-----|
| `0x00` | 4 B | `e_ident[0:4]` | Magic number: `0x7F 'E' 'L' 'F'` (`0x464C457F` in little-endian) |
| `0x04` | 1 B | `e_ident[4]` | Class: 1 = 32-bit, **2 = 64-bit** |
| `0x05` | 1 B | `e_ident[5]` | Data encoding: **1 = little-endian**, 2 = big-endian |
| `0x18` | 8 B | `e_entry` | Virtual address of the entry point (we treat the low 32 bits as physical) |
| `0x20` | 8 B | `e_phoff` | Offset from start of file to the program header table |
| `0x36` | 2 B | `e_phentsize` | Size of each program header entry (56 bytes for ELF64) |
| `0x38` | 2 B | `e_phnum` | Number of entries in the program header table |

We validate the magic, class, and endianness before proceeding. An invalid ELF
writes `'L'` (Loader error) to VGA and halts:

```nasm
  cmp dword [esi], 0x464C457F  ; "\x7FELF"
  jne elf_err
  cmp byte [esi + 4], 2     ; ELFCLASS64
  jne elf_err
  cmp byte [esi + 5], 1     ; little-endian
  jne elf_err
```

### 7c. ELF64 program header: the fields we read

Each program header is 56 bytes. The fields we read from a `PT_LOAD` entry:

| Offset | Size | Field | Use |
|--------|------|-------|-----|
| `0x00` | 4 B | `p_type` | 1 = `PT_LOAD` (loadable segment), skip others |
| `0x08` | 8 B | `p_offset` | Byte offset of segment data within the ELF file |
| `0x18` | 8 B | `p_paddr` | Physical address to load the segment at |
| `0x20` | 8 B | `p_filesz` | Bytes to copy from the file image |
| `0x28` | 8 B | `p_memsz` | Total bytes in memory (`≥ filesz`; excess is BSS) |

All 8-byte fields are read as their low 32 bits because the kernel is linked at
`0x100000` (1 MiB), which fits in 32 bits. A full 64-bit bootloader would need
to read the upper 32 bits as well, but we know our kernel's link address.

### 7d. The copy loop, step by step

```nasm
  mov eax, [esi + 0x20]     ; e_phoff (low 32)
  add eax, 0x10000       ; linear address of first PH
  mov ebx, eax         ; ebx = current PH pointer
  movzx ecx, word [esi + 0x36]  ; e_phentsize
  movzx edx, word [esi + 0x38]  ; e_phnum

.ph_loop:
  test edx, edx          ; any headers left?
  jz  .ph_done
  dec edx

  cmp dword [ebx], 1       ; PT_LOAD?
  jne .ph_next

  ; Copy segment: source = p_offset + 0x10000, dest = p_paddr
  mov esi, [ebx + 0x08]     ; p_offset (low 32)
  add esi, 0x10000       ; source = ELF base + offset
  mov edi, [ebx + 0x18]     ; p_paddr (low 32), destination
  mov ecx, [ebx + 0x20]     ; p_filesz (low 32), byte count
  cld
  rep movsb           ; copy ECX bytes from [ESI] to [EDI]

  ; Zero BSS: p_memsz - p_filesz bytes
  mov ecx, [ebx + 0x28]     ; p_memsz
  sub ecx, [ebx + 0x20]     ; p_filesz
  jz  .ph_copy_done       ; no BSS for this segment
  mov al, 0
  rep stosb           ; zero ECX bytes starting at EDI

  ; Save BSS bounds (last segment wins only one PT_LOAD in our kernel)
  mov eax, [ebx + 0x18]     ; p_paddr
  add eax, [ebx + 0x20]     ; + p_filesz → BSS start
  mov [__bss_start], eax
  mov eax, [ebx + 0x18]     ; p_paddr
  add eax, [ebx + 0x28]     ; + p_memsz → BSS end
  mov [__bss_end], eax

.ph_copy_done:
  ; ...

.ph_next:
  add ebx, ecx         ; advance to next PH (ecx = phentsize)
  jmp .ph_loop
```

`cld` (Clear Direction Flag) ensures `rep movsb` and `rep stosb` increment
`ESI`/`EDI` (forward direction). The BIOS may leave the Direction Flag set or
cleared; we cannot assume.

The BSS bounds are saved to `__bss_start` and `__bss_end`, two 64-bit variables
defined in `longmode.asm`. These are read by the 64-bit entry stub to zero the
BSS region of the loaded kernel. BSS holds uninitialized global variables: the C
standard guarantees they start as zero, so the bootloader must zero the memory
before calling `kernel_main`.

The ELF parser zeroes the BSS of each segment immediately after copying it, so
`__bss_start` and `__bss_end` store the address of the last (and typically only)
PT_LOAD segment's BSS. The 64-bit stub re-zeroes the entire BSS range using
these bounds a small redundancy that costs nothing.

### 7e. Saving the entry point

```nasm
  mov eax, [esi + 0x18]     ; e_entry (low 32 bits)
  mov [kernel_entry], eax
  mov eax, [esi + 0x1C]     ; e_entry (high 32 bits)
  mov [kernel_entry + 4], eax
```

`kernel_entry` is a `dq 0` (8 bytes) defined in `longmode.asm`. The 64-bit stub
loads and calls it.

**Source file:** `boot/stage2/loader.asm` (ELF parsing logic)

**Further reading:** <https://wiki.osdev.org/ELF>,
<https://wiki.osdev.org/ELF_Tutorial>

---

## 8. Entering long mode (`boot/stage2/longmode.asm`)

### 8a. The full sequence

Entering 64-bit long mode is a multi-step process because the CPU requires
several configuration steps to be performed in a specific order. Doing them in
the wrong order causes a `#GP` fault or a triple fault (CPU reset).

The sequence, executed by `enter_long_mode` (32-bit code):

1. **Disable 32-bit paging** (clear CR0.PG) safe because we are identity-mapped by the flat GDT; disabling paging changes nothing about how addresses resolve.
2. **Build 4-level PAE page tables** at `0x9000`–`0xBFFF` identity-map the first 2 MiB.
3. **Load CR3** with the PML4 base address (`0x9000`).
4. **Enable CR4.PAE** (bit 5) required before setting EFER.LME.
5. **Set EFER.LME** (MSR `0xC0000080`, bit 8) enables long mode.
6. **Enable CR0.PG** the CPU enters **compatibility mode** (32-bit protected mode with 64-bit capability, but still executing 32-bit code).
7. **Far jump `GDT_CODE64:long_mode_entry`** the CPU reloads CS with the L=1 descriptor and enters **64-bit long mode**.

After the far jump, `long_mode_entry` (64-bit code):

8. **Load data segments** (DS, ES, FS, GS, SS = `0x10`).
9. **Zero kernel BSS** using `__bss_start`/`__bss_end` saved by the ELF parser.
10. **Align RSP to 16 bytes** SysV AMD64 ABI requires `RSP % 16 == 0` before `call`.
11. **Load kernel entry point** from `kernel_entry` and `call rax`.

### 8b. Step 1 Disable any existing paging

```nasm
  mov eax, cr0
  and eax, ~CR0_PG       ; clear PG (bit 31)
  mov cr0, eax
```

Disabling paging is a no-op in our case (we never enabled it), but it is good
practice. If the loader were ever invoked from an environment that had already
set up paging, this step would prevent stale TLB entries and cache coherency
issues.

### 8c. Steps 2–3 Build 4-level PAE page tables

In long mode, every memory access goes through a page table walk. The CPU uses a
4-level hierarchy of tables. Each table is a 4 KiB page containing 512 8-byte
entries. An entry points either to the next-level table or, at the lowest level,
to a physical page frame.

The levels, from top to bottom:

| Level | Acronym | Table name | Indexed by virtual address bits |
|-------|---------|------------|--------------------------------|
| 4 | PML4 | Page-Map Level-4 Table | 47:39 |
| 3 | PDPT | Page-Directory Pointer Table | 38:30 |
| 2 | PD | Page Directory | 29:21 |
| 1 | PT | Page Table | 20:12 |

At the Page Directory level, we can set the PS (Page Size) bit (bit 7) to map a
**2 MiB huge page** instead of pointing to a 4 KiB page table. This is what we
do: one PD entry maps the first 2 MiB of physical memory as a single huge page.

The three tables are placed at hardcoded physical addresses:

| Table | Address | Size |
|-------|---------|------|
| PML4 | `0x9000` | 4 KiB |
| PDPT | `0xA000` | 4 KiB |
| PD | `0xB000` | 4 KiB |

We clear all three tables first (zero-fill with `rep stosd`), then write one
entry in each:

```nasm
  ; Clear PML4, PDPT, PD
  mov edi, PML4_ADDR
  mov ecx, 4096 / 4
  xor eax, eax
  rep stosd
  ; ... repeat for PDPT and PD ...

  ; PML4[0] → PDPT at 0xA000
  mov dword [PML4_ADDR],   PDPT_ADDR | PT_PRESENT | PT_WRITABLE
  mov dword [PML4_ADDR + 4], 0

  ; PDPT[0] → PD at 0xB000
  mov dword [PDPT_ADDR],   PD_ADDR | PT_PRESENT | PT_WRITABLE
  mov dword [PDPT_ADDR + 4], 0

  ; PD[0] → 2 MiB huge page at physical address 0x00000000
  mov dword [PD_ADDR],    PT_PRESENT | PT_WRITABLE | PT_HUGE
  mov dword [PD_ADDR + 4],  0
```

Each entry is 8 bytes. The low 32 bits hold the address of the next table (or
the page frame) and flags; the high 32 bits are zero because the physical
addresses we use (`0xA000`, `0xB000`, `0x00000000`) fit in 32 bits.

The flags used:

| Flag | Bit | Meaning |
|------|-----|---------|
| `PT_PRESENT` (0x1) | 0 | The page or table is present in memory. Without this, any access triggers `#PF`. |
| `PT_WRITABLE` (0x2) | 1 | The page is writable. Clear to make it read-only. |
| `PT_HUGE` (0x80) | 7 | At the PD level: this entry maps a 2 MiB page rather than pointing to a PT. |

After building the tables, we load CR3:

```nasm
  mov eax, PML4_ADDR
  mov cr3, eax
```

CR3 holds the **physical address** of the PML4 table. The CPU starts every
address translation by reading the entry at `CR3 + (virtual_bits[47:39] × 8)`.

### 8d. Step 4 Enable PAE (Physical Address Extension)

```nasm
  mov eax, cr4
  or  eax, CR4_PAE
  mov cr4, eax
```

PAE (Physical Address Extension) was introduced in the Pentium Pro (1995) to
allow 32-bit systems to address more than 4 GiB of physical memory. In long
mode, PAE is mandatory: 4-level paging is built on top of the PAE page table
format (8-byte entries instead of the 4-byte entries used by legacy 32-bit
paging). Without CR4.PAE, the CPU refuses to enter long mode.

### 8e. Step 5 Set EFER.LME (Long Mode Enable)

```nasm
  mov ecx, EFER_MSR      ; 0xC0000080
  rdmsr
  or  eax, EFER_LME      ; bit 8
  wrmsr
```

EFER (Extended Feature Enable Register) is a Model-Specific Register (MSR)
accessed via `RDMSR`/`WRMSR` with the MSR index in `ECX`. The data is split
across `EDX:EAX` (high 32 bits in EDX, low 32 bits in EAX). We only need to
set bit 8 (LME, Long Mode Enable) in the low 32 bits.

Setting LME does not immediately switch the CPU to long mode. It takes effect
only when paging is subsequently enabled (CR0.PG = 1). The CPU then checks LME:
if LME=1 and PAE=1 and PG=1, it enters compatibility mode. If LME=0 and PG=1,
it enters legacy 32-bit paging mode.

### 8f. Steps 6–7 Enable paging and enter long mode

```nasm
  mov eax, cr0
  or  eax, CR0_PG
  mov cr0, eax

  jmp GDT_CODE64:long_mode_entry
```

The moment CR0.PG is set, the CPU enters **compatibility mode**: it is still
executing 32-bit instructions, but paging is active and 64-bit segment
descriptors are valid. The far jump to `GDT_CODE64:long_mode_entry` (selector
`0x18`, the L=1 descriptor) causes the CPU to reload `CS` from the GDT, see
`L=1`, and switch to full 64-bit long mode.

### 8g. Step 8–11 64-bit long mode: final setup and kernel call

```nasm
[BITS 64]

long_mode_entry:
  mov ax, GDT_DATA
  mov ds, ax
  mov es, ax
  mov fs, ax
  mov gs, ax
  mov ss, ax

  ; Zero BSS using bounds from ELF parser
  mov eax, __bss_start
  mov rdi, [rax]         ; RDI = BSS start
  mov eax, __bss_end
  mov rcx, [rax]         ; RCX = BSS end
  sub rcx, rdi          ; RCX = byte count
  jz  .bss_done
  xor eax, eax
  rep stosb
.bss_done:

  ; Align stack to 16 bytes
  and rsp, ~0xF

  ; Call kernel entry
  mov eax, kernel_entry
  mov rax, [rax]
  call rax            ; → kernel_main()
```

The BSS zeroing uses the bounds saved by the ELF64 parser. `__bss_start` and
`__bss_end` are labels in `longmode.asm` themselves (each `dq 0`), so to read
their values we must load their addresses into a register first (`mov eax,
__bss_start`), then dereference (`mov rdi, [rax]`). This is because the
labels hold the addresses the ELF parser stored, not the bounds themselves.

The stack alignment (`and rsp, ~0xF`) rounds RSP down to the nearest 16-byte
boundary. At this point RSP is `0x8` mod 16 because the 32-bit `call
enter_long_mode` pushed a 4-byte return address onto the 64-bit stack. The ABI
requires 16-byte alignment at the point of `call`, so we fix the alignment
before calling the kernel.

### 8h. Summary: the paging structure we built

```
Virtual address 0x00000000_00000000
  ↓
PML4[0] @ 0x9000 → PDPT @ 0xA000
  ↓
PDPT[0] @ 0xA000 → PD @ 0xB000
  ↓
PD[0] @ 0xB000 → 2 MiB huge page @ physical 0x00000000_00000000
```

This identity-maps the first 2 MiB of physical memory. The kernel at `0x100000`
(1 MiB) is within this range, as are the VGA buffer (`0xB8000`), the page tables
themselves (`0x9000`–`0xBFFF`), and the memory-mapped I/O region below
`0x100000`.

The identity mapping (virtual = physical) means addresses in our code and data
work identically whether paging is on or off. This is essential during the
transition: after enabling paging, the next instruction fetch still works
because `0x100000` resolves to `0x100000` through the page tables.

| Register | Value | Meaning |
|----------|-------|---------|
| EFER.LMA | 1 | Long Mode Active (read-only, CPU sets this when LME=PG=1) |
| CS.L | 1 | 64-bit code segment (loaded from the GDT descriptor with L=1) |
| CR0.PG | 1 | Paging enabled |
| CR4.PAE | 1 | PAE active (required for 4-level paging) |
| CR3 | `0x9000` | PML4 base address |

**Source file:** `boot/stage2/longmode.asm`

**Further reading:** <https://wiki.osdev.org/Long_Mode>,
<https://wiki.osdev.org/Setting_Up_Long_Mode>,
<https://wiki.osdev.org/Paging>, <https://wiki.osdev.org/Page_Tables>

---

## 9. The kernel (`kernel/kernel.c` + `kernel/linker.ld`)

### 9a. Build configuration

The kernel is compiled as a 64-bit freestanding binary. "Freestanding" means
no standard library (`-nostdlib`, `-ffreestanding`): functions like `printf` and
`malloc` do not exist. The kernel must provide its own output routines (VGA
writes) and memory management.

Key compilation flags:

| Flag | Purpose |
|------|---------|
| `-m64` | Generate 64-bit code |
| `-mcmodel=large` | Use 64-bit absolute addresses (kernel is linked above 2 GiB in the large model, but at 1 MiB this is conservative) |
| `-mno-red-zone` | Disable the red zone (128 bytes below RSP used by user-space for leaf functions). Cannot be used in kernel: an interrupt would overwrite the red zone. |
| `-ffreestanding` | Do not assume the C standard library is present |
| `-nostdlib` | Do not link against `libc` |
| `-fno-pic` | Disable position-independent code (we know our link address) |
| `-fno-stack-protector` | Disable stack canaries (require libc support) |
| `-O2` | Optimize for speed (reasonable default; `-Os` for size is also valid) |

### 9b. Linker script (`kernel/linker.ld`)

The linker script places the kernel at `0x100000` (1 MiB):

```ld
ENTRY(kernel_main)

SECTIONS
{
  . = 0x100000;

  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
  }
}
```

The script exports three symbols:

| Symbol | Value |
|--------|-------|
| `kernel_main` | Address of the C entry point (the `ENTRY` directive) |
| `__bss_start` | Start of the `.bss` section in the linked binary |
| `__bss_end` | End of the `.bss` section |

`__bss_start` and `__bss_end` are **linker symbols**: they appear in the ELF
symbol table, and their values (virtual addresses) are what the ELF64 parser in
the loader copies to the variables in `longmode.asm`. The 64-bit entry stub
reads those variables and zeroes the memory between the two addresses.

The order of sections matters for the BSS: `.bss` comes last, after `.data`, so
the BSS occupies the highest addresses within the kernel binary. The linker
symbols `__bss_start` and `__bss_end` mark the boundaries of this section.

### 9c. The kernel entry point

The current `kernel_main` is a minimal program:

```c
void kernel_main() {
  vga_init();
  vga_draw_heart(20, 2); // kernel logo: heart in the middle of the screen
  for (;;) {}       // kernel never stops
}
```

`vga_init()` clears the screen (fills with spaces using the current color, white
on black) and resets the cursor to `(0, 0)`. Without this call, the screen would
show whatever garbage the BIOS left in the VGA buffer.

`vga_draw_heart()` renders a 40×20 ASCII-art heart using the mathematical heart
curve equation: `(x² + y² − 1)³ ≤ x² · y³`. The coordinates are scaled to a
fixed-point range to fit the character grid. The heart is placed at column 20,
row 2, roughly centered on an 80×25 screen.

The `for (;;) {}` infinite loop prevents the kernel from "falling off the end"
of `kernel_main`. Since `kernel_main` is called via `call`, a return from it
would pop an invalid return address from the stack and jump to a random address,
likely causing a triple fault.

**Source files:** `kernel/kernel.c`, `kernel/linker.ld`

**Further reading:** <https://wiki.osdev.org/Printing_To_Screen>,
<https://wiki.osdev.org/VGA_Hardware>, <https://wiki.osdev.org/C_PlusPlus_bare_bones>

---

## 10. VGA text output (`kernel/drivers/vga.c`, `vga.h`)

### 10a. The VGA buffer

The VGA text-mode buffer lives at physical address `0xB8000`. It is an array of
80 columns × 25 rows = 2000 cells. Each cell is 2 bytes (16 bits):

| Byte | Bits | Purpose |
|------|------|---------|
| 0 (even offset) | 7:0 | ASCII character code (the glyph to display) |
| 1 (odd offset) | 3:0 | Foreground color (4 bits, 16 colors) |
| 1 (odd offset) | 6:4 | Background color (3 bits when bit 7 is used for blink; 4 bits otherwise) |
| 1 (odd offset) | 7 | Blink (if enabled via VGA register) or bright background (default) |

The attribute byte is constructed as `fg | (bg << 4)`:

```c
uint8_t vga_make_color(enum vga_color fg, enum vga_color bg) {
  return fg | (bg << 4);
}

uint16_t vga_entry(char c, uint8_t attr) {
  return (uint16_t)c | ((uint16_t)attr << 8);
}
```

For example, white text (`0x0F`) on a red background (`0x04`) produces attribute
byte `0x4F`. Writing character `'A'` (`0x41`) with this attribute produces the
16-bit entry `0x4F41`.

### 10b. The VGA color palette

VGA text mode provides 16 colors, controlled by 4 bits per channel:

| Value | Color | Value | Color |
|-------|-------|-------|-------|
| 0 | Black | 8 | Dark Gray |
| 1 | Blue | 9 | Light Blue |
| 2 | Green | 10 (0xA) | Light Green |
| 3 | Cyan | 11 (0xB) | Light Cyan |
| 4 | Red | 12 (0xC) | Light Red |
| 5 | Magenta | 13 (0xD) | Light Magenta |
| 6 | Brown | 14 (0xE) | Yellow |
| 7 | Light Gray | 15 (0xF) | White |

The lower 8 colors (0–7) are the standard CGA palette. The upper 8 colors (8–15)
are the "bright" variants, distinguished by the intensity bit (bit 3 of the
nibble). Color 6 (brown) is an exception: on the original CGA, it displayed as
brown, but on EGA/VGA hardware, setting bit 3 changes it to yellow (0xE) rather
than "bright brown."

### 10c. Cursor management and newline handling

The driver maintains a `cursor_position` struct (column `x`, row `y`). When a
character is written, `x` increments. When `x` reaches `VGA_WIDTH` (80), it wraps
to column 0 and `y` increments. The cursor stops at row `VGA_HEIGHT - 1` (24,
the last row) there is no scrolling yet.

The newline character `'\n'` resets column to 0 and increments row. It does not
write anything to the VGA buffer: moving to the next line is a purely logical
operation on the cursor position.

### 10d. Hexadecimal output

`vga_puthex(uint64_t value)` prints a 64-bit value as 16 uppercase hex digits
prefixed with `0x`:

```c
void vga_puthex(uint64_t value) {
  vga_putchar('0');
  vga_putchar('x');
  for (int i = 15; i >= 0; i--) {
    uint8_t nibble = (value >> (i * 4)) & 0xF;
    vga_putchar("0123456789ABCDEF"[nibble]);
  }
  vga_putchar('\n');
}
```

This function is essential for debugging: exception handlers (added in M2) use
it to dump register values, and future memory-management code will use it to
print physical addresses.

**Source files:** `kernel/drivers/vga.c`, `kernel/drivers/vga.h`

**Further reading:** <https://wiki.osdev.org/Printing_To_Screen>,
<https://wiki.osdev.org/VGA_Hardware>

---

## 11. Disk layout

The bootable floppy image is built by `dd` in the Makefile:

| LBA | Content | Source | Size |
|-----|---------|--------|------|
| 0 | Stage 1 bootsector | `boot/stage1/boot.asm` | 512 B |
| 1–8 | Stage 2 loader | `boot/stage2/loader.asm` + includes | up to 4 KiB (8 × 512) |
| 9–72 | Kernel ELF | All `.c` and `.asm` sources compiled and linked | up to 32 KiB (64 × 512) |

The image is a 1.44 MiB floppy (2880 sectors of 512 bytes), created with:

```makefile
$(OS_IMG): $(BOOT_BIN) $(LOADER_BIN) $(KERNEL_ELF)
  dd if=/dev/zero of=$@ bs=512 count=2880
  dd if=$(BOOT_BIN)  of=$@ bs=512 count=1 conv=notrunc
  dd if=$(LOADER_BIN) of=$@ bs=512 seek=1 conv=notrunc
  dd if=$(KERNEL_ELF) of=$@ bs=512 seek=9 conv=notrunc
```

`conv=notrunc` prevents `dd` from truncating the output file after writing.
Without it, writing the kernel would shrink the image to 9 + kernel_size
sectors, losing the empty sectors beyond.

The kernel at 32 KiB is generous for now but will need to grow as more
subsystems are added. When it exceeds 32 KiB, the loader's sector count must be
increased.

---

## 12. Memory map at kernel entry

```
0x000000 - 0x000FFF  Real-mode Interrupt Vector Table (IVT), dead after protected-mode switch
0x000400 - 0x0004FF  BIOS Data Area (BDA), dead after protected-mode switch
0x000500 - 0x00051F  A20 test locations (restored after check, safe to reuse)
0x007C00 - 0x007DFF  Stage 1 bootsector (dead, stack overwrites low addresses)
0x008000 - 0x008FFF  Stage 2 loader code (dead after jump to kernel)
0x009000 - 0x009FFF  PML4 table (active, 1 of 512 entries used)
0x00A000 - 0x00AFFF  PDPT table (active, 1 of 512 entries used)
0x00B000 - 0x00BFFF  PD table (active, 1 of 512 entries used)
0x010000 - 0x017FFF  Kernel ELF buffer (dead after segments copied to link address)
0x090000        Stack top (grows down toward 0x010000, 512 KiB available)
0x0B8000        VGA text-mode buffer (80 × 25 × 2 bytes = 4000 bytes)
0x100000        Kernel .text (entry point), .rodata, .data, .bss
```

"Dead" memory means the data in that range is no longer needed and the space can
be reused. The page tables at `0x9000`–`0xBFFF` are the only boot-state memory
that survives into kernel execution, and they will be replaced when the kernel
sets up its own virtual memory system.

---

## 13. The build pipeline (Makefile)

The build process assembles and links everything into a single 1.44 MiB floppy
image:

```
boot.asm ──[nasm -f bin]──→ boot.bin
loader.asm + includes ──[nasm -f bin]──→ loader.bin
isr_stubs.asm ──[nasm -f elf64]──→ isr_stubs.o
 *.c ──[gcc -m64 -ffreestanding]──→ *.o
 all .o ──[ld -melf_x86_64 -T linker.ld]──→ kernel.elf
 boot.bin + loader.bin + kernel.elf ──[dd]──→ os.img (2880 sectors)
```

Two assemblers are used: `nasm -f bin` for the 16/32-bit boot code (which must be
raw binary, not ELF, because the BIOS loads it directly), and `nasm -f elf64`
for the 64-bit ISR stubs (which are linked into the kernel ELF with the C code).

The C compiler produces ELF64 object files. They are linked with `ld
-melf_x86_64` using the linker script at `kernel/linker.ld`, which places the
kernel at `0x100000`.

---

## 14. Source files

| File | Purpose | BITS/ISA |
|------|---------|----------|
| `boot/stage1/boot.asm` | Stage 1 bootsector (CHS load, 512 bytes, `0xAA55` signature) | 16 |
| `boot/stage2/loader.asm` | Stage 2: CHS load, A20, GDT, CPUID, ELF64 parse, long mode call | 16→32 |
| `boot/stage2/a20.asm` | A20 check (wraparound trick) + enable chain (BIOS, 8042, Fast A20) | 16 |
| `boot/stage2/gdt.asm` | GDT: null (0x00), code32 (0x08), data (0x10), code64 (0x18) | 16 |
| `boot/stage2/cpuid.asm` | CPUID detection (EFLAGS bit 21 toggle) + long mode feature check | 32 |
| `boot/stage2/longmode.asm` | 4-level PAE tables, CR3/CR4.PAE/EFER.LME, far jump to long mode, BSS zero, call kernel | 32→64 |
| `kernel/kernel.c` | C entry point: VGA init, heart logo, infinite loop | C, `-m64` |
| `kernel/drivers/vga.c` | VGA text-mode driver: putchar, puts, puthex, clear, heart rendering | C, `-m64` |
| `kernel/drivers/vga.h` | VGA constants, cursor struct, color enum, driver API | C header |
| `kernel/linker.ld` | Link at `0x100000`, export `__bss_start`/`__bss_end` | Linker script |
| `kernel/lib/stdint.h` | Fixed-width integer types (`uint8_t`..`uint64_t`) | C header |
| `kernel/lib/stddef.h` | `size_t`, `NULL` | C header |
| `Makefile` | Build pipeline: nasm, gcc, ld, dd | Make |

---

## 15. References

- **Real mode:** <https://wiki.osdev.org/Real_Mode>
- **Boot sequence:** <https://wiki.osdev.org/Boot_Sequence>
- **A20 gate:** <https://wiki.osdev.org/A20_Line>
- **Protected mode:** <https://wiki.osdev.org/Protected_Mode>
- **GDT:** <https://wiki.osdev.org/GDT>, <https://wiki.osdev.org/GDT_Tutorial>
- **CPUID:** <https://wiki.osdev.org/CPUID>
- **Long mode:** <https://wiki.osdev.org/Long_Mode>, <https://wiki.osdev.org/Setting_Up_Long_Mode>
- **Paging:** <https://wiki.osdev.org/Paging>, <https://wiki.osdev.org/Page_Tables>
- **ELF format:** <https://wiki.osdev.org/ELF>, <https://wiki.osdev.org/ELF_Tutorial>
- **VGA text mode:** <https://wiki.osdev.org/Printing_To_Screen>, <https://wiki.osdev.org/VGA_Hardware>
- **C bare bones:** <https://wiki.osdev.org/C_PlusPlus_bare_bones>
- **Linker scripts:** <https://wiki.osdev.org/Linker_Scripts>
