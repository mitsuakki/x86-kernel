# M2 Interrupts

**Goal:** CPU exception handling with register dump, 8259A PIC remap so hardware
IRQs land on safe vectors, and an interrupt smoke test to prove the chain works
end-to-end.

**Status:**
- [x] IDT structure definition + LIDT load (`kernel/arch/x86_64/idt.h`, `idt.c`)
- [x] ISR assembly stubs for CPU exceptions 0–31 (`kernel/arch/x86_64/isr_stubs.asm`)
- [x] Exception handler dispatch + panic screen with register dump (`kernel/arch/x86_64/isr.c`)
- [x] I/O port primitives (`kernel/lib/io.h` `inb`, `outb`, `io_wait`)
- [x] 8259A PIC remap (`kernel/arch/x86_64/pic.c`, `pic.h`)
- [x] Interrupt smoke test (`int $0x3` in `kernel/kernel.c`)

---

## 1. Why interrupts matter

An operating system is not a batch script that runs from top to bottom and exits.
It must react to events it did not schedule: a key is pressed, a timer fires, a
network packet arrives, a program divides by zero. Interrupts are the mechanism
the CPU provides to pause normal execution, run a handler, and resume where it
left off.

There are two families of interrupts:

| Family | Source | Examples |
|--------|--------|----------|
| **Exceptions** | The CPU itself, synchronously | Division by zero, page fault, invalid opcode |
| **Hardware IRQs** | External devices, asynchronously | Keyboard, timer, disk controller |

Both families share the same dispatch mechanism: the **Interrupt Descriptor
Table (IDT)**. The CPU looks up a vector number (0–255) in the IDT, finds the
address of the handler, and calls it. Vectors 0–31 are reserved for CPU
exceptions; vectors 32–255 can be used by hardware IRQs and software interrupts.

Without interrupts, an OS is blind. With them, it can respond to the outside
world.

**Further reading:** <https://wiki.osdev.org/Interrupts>,
<https://wiki.osdev.org/Exceptions>

---

## 2. The Interrupt Descriptor Table IDT

### 2a. What the IDT looks like

The IDT is a 256-entry array in memory. Each entry is a 16-byte descriptor that
describes one interrupt handler. Think of it as a phonebook: the vector number
is the name, and the entry tells the CPU which number to dial that is, which
function to call.

Each entry has this layout (64-bit mode, 16 bytes total):

| Bits | Size | Field | Meaning |
|------|------|-------|---------|
| 0–15 | 2 B | ISR low | Lower 16 bits of the handler address |
| 16–31 | 2 B | Kernel CS | GDT code segment selector (we use `0x18`, the 64-bit code segment) |
| 32–35 | 4 b | IST | Interrupt Stack Table index (0 = use current stack) |
| 40–43 | 4 b | Gate type | `0xE` = interrupt gate, `0xF` = trap gate |
| 44 | 1 b | Storage | Must be 0 |
| 45–46 | 2 b | DPL | Descriptor Privilege Level (0 = kernel-only, 3 = user-callable) |
| 47 | 1 b | Present | 1 = entry is valid |
| 48–63 | 2 B | ISR mid | Middle 16 bits of the handler address |
| 64–95 | 4 B | ISR high | Upper 32 bits of the handler address |
| 96–127 | 4 B | Reserved | Must be 0 |

The address is split across three fields because the x86 architecture grew
incrementally: the 16-bit 286 had a 2-byte address field, the 32-bit 386 added
another 2 bytes, and x86-64 added 4 more. Rather than redesign the structure,
Intel simply split the address across leftover space.

In C, the descriptor looks like this (`kernel/arch/x86_64/idt.h`):

```c
typedef struct {
  uint16_t isr_low;    // lower 16 bits of handler address
  uint16_t kernel_cs;   // GDT code segment selector (0x18)
  uint8_t ist;      // Interrupt Stack Table index
  uint8_t attributes;  // gate type, DPL, present bit
  uint16_t isr_mid;    // middle 16 bits of handler address
  uint32_t isr_high;   // upper 32 bits of handler address
  uint32_t reserved;   // must be 0
} __attribute__((packed)) idt_entry_t;
```

The `__attribute__((packed))` is critical: the compiler would otherwise insert
padding between fields to satisfy alignment rules, and the CPU would
misinterpret the bytes.

### 2b. Interrupt gates vs. trap gates

Two gate types matter at this stage:

| Gate type | Attribute byte | IF flag on entry | Used for |
|-----------|----------------|-------------------|----------|
| Interrupt gate | `0x8E` | CPU **clears** IF (disables maskable interrupts) | Hardware IRQs |
| Trap gate | `0x8F` | CPU **leaves IF alone** | CPU exceptions, syscalls |

Why the distinction? When a hardware interrupt fires and its handler starts
running, another device could fire immediately nesting interrupts on the same
stack without limit leads to stack overflow. The interrupt gate prevents this by
clearing IF, blocking other maskable interrupts until the handler finishes or
explicitly re-enables them with `sti`.

CPU exceptions, on the other hand, fire because of the instruction currently
executing. Nesting is not the same concern, so trap gates leave IF untouched.

Both gate types use DPL 0 (kernel-only). A DPL 3 gate would let user-mode code
trigger the vector with `int $N` necessary for system calls later, but not
something we want for exception handlers.

**Further reading:** <https://wiki.osdev.org/IDT>,
<https://wiki.osdev.org/Interrupt_Descriptor_Table>

### 2c. Loading the IDT with LIDT

The CPU needs to know where the IDT lives and how large it is. This information
is packed into a 10-byte structure called the **IDTR** (Interrupt Descriptor
Table Register):

```c
typedef struct {
  uint16_t limit;  // size of IDT in bytes minus 1
  uint64_t base;   // physical address of IDT entry 0
} __attribute__((packed)) idtr_t;
```

The `limit` is `256 × 16 − 1 = 4095`. A smaller limit means the CPU will
reject vectors whose index falls past the boundary with a `#GP` fault
accidental defense against unregistered vectors.

The `lidt` instruction loads the IDTR from this struct:

```c
__asm__ volatile ("lidt %0" :: "m"(idtr));
```

After `lidt`, the interrupt system is armed: the CPU can dispatch vectors 0–31
to the handlers we just registered. But hardware IRQs (vectors 32+) would still
arrive on their factory-default vectors, which overlap with CPU exceptions. We
handle that in Section 5 with the PIC remap.

### 2d. Initialization flow

`idt_init()` in `kernel/arch/x86_64/idt.c` does three things:

1. Set up the IDTR with the base address and limit of our 256-entry array.
2. For each vector 0–31, call `idt_set_descriptor()` to fill one IDT entry.
  The handler address comes from the `isr_stub_table[]` array, exported by
  the assembly stubs (Section 3).
3. Execute `lidt` to activate the table.

The 256-entry IDT is statically allocated with 16-byte alignment (`aligned(0x10)`).
Alignment is required by the hardware: `lidt` loads the IDTR and the CPU reads
entries at `base + vector × 16`. If the base is not 16-byte aligned, those
calculations are off.

**Source files:** `kernel/arch/x86_64/idt.h`, `kernel/arch/x86_64/idt.c`

**Further reading:** <https://wiki.osdev.org/IDT>

---

## 3. ISR stubs the assembly entry points (`kernel/arch/x86_64/isr_stubs.asm`)

### 3a. Why stubs are needed

The CPU knows it must jump to the handler address stored in the IDT entry for a
given vector. But it does not tell the handler **which** vector triggered it
it just pushes some state onto the stack, disables interrupts (for interrupt
gates), and jumps. The handler must save the complete register file, record the
vector number, and call the C-level handler. That is what the stub does.

We could write 32 near-identical stubs by hand. Instead, we use two NASM macros
to generate them: one for exceptions that push an error code, one for exceptions
that do not.

### 3b. Error codes

Not all exceptions push an error code. Of the 32 CPU-defined vectors, 10
include an error code:

| Vector | Mnemonic | Name | Error code pushed? |
|--------|----------|------|---------------------|
| 8 | `#DF` | Double Fault | Yes (always 0) |
| 10 | `#TS` | Invalid TSS | Yes |
| 11 | `#NP` | Segment Not Present | Yes |
| 12 | `#SS` | Stack-Segment Fault | Yes |
| 13 | `#GP` | General Protection Fault | Yes |
| 14 | `#PF` | Page Fault | Yes |
| 17 | `#AC` | Alignment Check | Yes (always 0) |
| 21 | `#CP` | Control Protection | Yes |
| 29 | `#VC` | VMM Communication | Yes |
| 30 | `#SX` | Security Exception | Yes |

For the other 22 vectors, the CPU pushes only `[RFLAGS, CS, RIP]`. If we left
the stack inconsistent 3 values for no-error exceptions, 4 for with-error
exceptions the C handler would read garbage when it tries to decode the frame.
The solution: **push a dummy error code of 0 for no-error exceptions**, so every
exception arrives at the common handler with the identical stack layout.

### 3c. The two macros

```nasm
; Exceptions WITHOUT error code: push dummy 0 for uniform stack layout
%macro isr_no_err_stub 1
isr_stub_%+%1:
  push 0       ; dummy error code
  push %1       ; interrupt number
  jmp isr_common_stub
%endmacro

; Exceptions WITH error code: CPU already pushed it, just push vector number
%macro isr_err_stub 1
isr_stub_%+%1:
  push %1       ; interrupt number
  jmp isr_common_stub
%endmacro
```

Both macros push the interrupt number on top of the error code (real or dummy).
Then they jump to `isr_common_stub`.

### 3d. The common stub save, call, restore, return

`isr_common_stub` performs four steps:

**Step 1 Save general-purpose registers (`push` in order):**

```nasm
isr_common_stub:
  push rax
  push rbx
  push rcx
  push rdx
  push rsi
  push rdi
  push rbp
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
```

The push order is not arbitrary. It must match the field order of the
`registers_t` struct defined in `isr.h` **(first pushed = highest address =
last struct field)**. A mismatch means the C handler reads `r15` when it thinks
it is reading `rax` silent corruption that is painful to debug.

The `r9` and `r8` registers are saved before `r10`–`r15` because the System V
AMD64 ABI passes arguments 5 and 6 in `r8` and `r9` respectively. If the C
handler ever wanted to inspect the original calling-convention arguments, those
positions need to be known.

**Step 2 Call the C handler:**

```nasm
  mov rdi, rsp    ; RDI = pointer to registers_t on the stack
  call isr_handler
```

Under the System V AMD64 calling convention, the first argument to a function
goes in `rdi`. By setting `rdi = rsp`, we pass a pointer to the saved register
frame directly to `isr_handler(registers_t *r)`. No copying, no heap allocation
 the stack is the structure.

**Step 3 Restore general-purpose registers (`pop` in reverse order):**

```nasm
  pop r15
  pop r14
  ...
  pop rax
```

Reverse of the push order a stack is LIFO (Last In, First Out).

**Step 4 Clean up the stack and return:**

```nasm
  add rsp, 16     ; skip over int_no (8 bytes) + err_code (8 bytes)
  iretq
```

`iretq` is the 64-bit interrupt return. It pops `[RIP, CS, RFLAGS]` from the
stack and resumes execution at the faulting instruction (or the one after it,
for traps). Because the CPU pushed those three values when it took the
exception, they are still on the stack below error code and int_no. The `add
rsp, 16` skips past our two manually-pushed quadwords, exposing the IRET frame
to `iretq`.

### 3e. The stub table

At the bottom of the file, 32 `dq` (define quadword) directives create an array
of 64-bit pointers:

```nasm
global isr_stub_table
isr_stub_table:
%assign i 0
%rep  32
  dq isr_stub_%+i
%assign i i+1
%endrep
```

The C code declares `extern void* isr_stub_table[]` and indexes it:
`isr_stub_table[14]` is the address of `isr_stub_14`, the page fault stub.

### 3f. Full stack layout after the common stub runs

From the C handler's perspective (`rsp` = `registers_t *`):

```
[rflags]    ← CPU IRET frame (popped by iretq)
[cs]      ← "
[rip]      ← " (resume point after handler)
[err_code]   ← CPU error code, or dummy 0 pushed by stub
[int_no]    ← vector number, pushed by stub
[rax]      ← 1st push in isr_common_stub
[rbx]
[rcx]      (System V arg4)
[rdx]      (System V arg3)
[rsi]      (System V arg2)
[rdi]      (System V arg1)
[rbp]
[r8]      (System V arg5)
[r9]      (System V arg6)
[r10]
[r11]
[r12]
[r13]
[r14]
[r15]      ← last push, lowest address. RSP points here → registers_t.r15
```

The `registers_t` struct in `isr.h` declares fields in exactly this order
(bottom-to-top in the diagram above first field = `r15`, last field =
`rflags`), with `__attribute__((packed))` to prevent the compiler from inserting
padding.

**Further reading:** <https://wiki.osdev.org/Interrupt_Service_Routines>,
<https://wiki.osdev.org/Exceptions>

---

## 4. Exception handler dispatch + panic screen (`kernel/arch/x86_64/isr.c`)

### 4a. The `isr_handler` function

When an exception fires, `isr_common_stub` calls `isr_handler(registers_t *r)`.
This function is the kernel's **panic screen**: it prints what went wrong and
all register values to the VGA display, then halts the CPU.

### 4b. Exception names

A static string table maps vector numbers to human-readable names:

```c
static const char *const exception_names[32] = {
  "#DE Division Error",
  "#DB Debug",
  "Non-maskable Interrupt",
  "#BP Breakpoint",
  ...
  "#SX Security Exception",
  "Reserved",
};
```

The names use Intel's standard two-letter mnemonics (e.g., `#PF` for Page Fault,
`#GP` for General Protection Fault) so the output is recognizable to anyone
reading Intel or AMD manuals.

### 4c. Register dump

The `DUMP` macro prints a register's name followed by its 64-bit value in hex:

```c
#define DUMP(reg) do { \
  vga_puts(#reg "="); vga_puthex(r->reg); \
} while(0)
```

The C preprocessor's stringification operator (`#reg`) turns the argument into a
string literal: `DUMP(rax)` expands to `vga_puts("rax="); vga_puthex(r->rax);`.

All 20 fields of `registers_t` are dumped: 15 general-purpose registers, `rip`,
`rflags`, `cs`, `rsp`, `ss`, `int_no`, and `err_code`. The order is chosen so
that the most useful debugging values (`rax`, `rip`, `rflags`) appear near the
top.

### 4d. Page fault special case CR2

The Page Fault exception (`#PF`, vector 14) is the most common exception during
OS development because it fires whenever the CPU walks a page table and finds a
missing or misconfigured entry. When it fires, the CPU stores the **faulting
linear address** in control register CR2. The handler reads and prints it:

```c
if (r->int_no == EXCEPTION_PAGE_FAULT) {
  uint64_t cr2;
  __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
  vga_puts("cr2="); vga_puthex(cr2);
}
```

Knowing CR2 tells the developer which memory access caused the fault essential
for debugging page table bugs.

### 4e. Halting after a fault

```c
__asm__ volatile ("cli; hlt");
__builtin_unreachable();
```

`cli` clears the IF flag, disabling maskable interrupts. `hlt` stops the CPU
until the next interrupt but since interrupts are disabled, the CPU will never
wake. The kernel stops. This is a **panic**: the kernel cannot recover from a
CPU exception, so it prints the state and dies, leaving the evidence on screen
for the developer.

`__builtin_unreachable()` tells GCC that execution never continues past this
point. Without it, the compiler would warn about a `noreturn` function that
fell off the end, and might generate incorrect code for paths it assumes are
reachable.

The function is marked `__attribute__((noreturn))` so callers know it never
returns.

### 4f. Notes on missing handlers

Currently, `isr_handler` is the only C-level handler. All 32 exception vectors
point to their own stub, but all stubs call the same `isr_handler`. There is no
recovery logic a division by zero crashes the kernel just as hard as a double
fault. Future milestones will add per-exception handling (e.g., SIGFPE
emulation, page fault recovery) and separate IRQ handlers for hardware devices.

**Source files:** `kernel/arch/x86_64/isr.c`, `kernel/arch/x86_64/isr.h`

**Further reading:** <https://wiki.osdev.org/Exceptions>,
<https://wiki.osdev.org/Panic>

---

## 5. The 8259A Programmable Interrupt Controller PIC

### 5a. Why we need to reprogram the PIC

The 8259A PIC is a chip (actually two chips, master and slave, cascaded) that
sits between hardware devices and the CPU. When a device signals an interrupt,
the PIC prioritizes it and sends the corresponding interrupt vector number to
the CPU.

The problem: the PIC's factory-default vector ranges are **0x08–0x0F for the
master** and **0x70–0x77 for the slave**. Vector 0x08 overlaps with `#DF`
(Double Fault, vector 8). Vectors 0x0A–0x0D overlap with `#TS`, `#NP`, `#SS`,
and `#GP`. Vector 0x0E overlaps with `#PF` (Page Fault). If a keyboard
interrupt fires on its default vector, the CPU would dispatch the page fault
handler and the handler would dump garbage registers and halt the kernel.

We must **remap** the PIC so hardware IRQs use vectors 32–47, safely above the
CPU exception range.

### 5b. Dual-PIC architecture

A single 8259A handles 8 IRQ lines. Since the IBM PC/AT, systems have two
PICs cascaded together to handle 15 lines (the slave is wired to master IRQ2,
which eats one line 8 + 8 − 1 = 15):

| PIC | IRQ lines | Default vectors | I/O ports |
|-----|-----------|-----------------|-----------|
| Master | IRQ0–IRQ7 | 0x08–0x0F | Command: `0x20`, Data: `0x21` |
| Slave | IRQ8–IRQ15 | 0x70–0x77 | Command: `0xA0`, Data: `0xA1` |

Common IRQ assignments in a PC:

| IRQ | Device |
|-----|--------|
| 0 | PIT (Programmable Interval Timer) |
| 1 | PS/2 Keyboard |
| 2 | Slave PIC cascade |
| 3 | COM2 serial port |
| 4 | COM1 serial port |
| 5 | LPT2 / Sound card |
| 6 | Floppy disk controller |
| 7 | LPT1 parallel port |
| 8 | RTC (Real-Time Clock) |
| 9 | ACPI / free |
| 10 | Free / SCSI |
| 11 | Free / SCSI |
| 12 | PS/2 Mouse |
| 13 | FPU / Coprocessor |
| 14 | Primary ATA (hard disk) |
| 15 | Secondary ATA (CD-ROM) |

### 5c. I/O port primitives (`kernel/lib/io.h`)

Before we can talk to the PIC, we need a way to read and write I/O ports. On
x86, port I/O uses a separate address space from memory, accessed via the `in`
and `out` instructions:

```c
static inline void outb(uint16_t port, uint8_t value) {
  __asm__ volatile ("outb %b0, %w1" :: "a"(value), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port) {
  uint8_t value;
  __asm__ volatile ("inb %w1, %b0" : "=a"(value) : "Nd"(port) : "memory");
  return value;
}
```

The `%b0` and `%w1` operands tell GCC to use the byte-sized and word-sized
sub-registers (`al` and `dx`) for the value and port. The `"Nd"` constraint
allows an immediate constant (`N`) or a register (`d` = `dx`), which lets the
compiler optimize `outb(0x20, val)` into an immediate form without loading the
port into `dx` first.

`io_wait()` writes zero to port `0x80`, an unused diagnostic port on most PCs.
This gives the PIC enough time (roughly 1–4 µs) to process the previous command
before the next one arrives. It is not strictly needed on QEMU's emulated PIC
but costs nothing and ensures compatibility with real hardware.

```c
static inline void io_wait(void) {
  outb(0x80, 0);
}
```

### 5d. The remap sequence (ICW1 → ICW2 → ICW3 → ICW4)

The 8259A expects a four-step initialization sequence called ICW (Initialization
Command Words). This is a ritual inherited from the original 8259A design in
1980 the chip has no hardware reset for its vector base, so software must
perform this dance on every boot.

**Step 0 Save current masks:**

```c
uint8_t saved_master = inb(MASTER_DATA);  // read master IMR
uint8_t saved_slave = inb(SLAVE_DATA);  // read slave IMR
```

The PIC boots with both masks set to `0xFF` (all IRQs masked). We want to
preserve whatever mask state exists in case the BIOS or bootloader already
configured something.

**Step 1 ICW1: "Start initialization, we will send ICW4":**

```c
outb(MASTER_COMMAND, ICW1_INIT | ICW1_ICW4); // 0x11
io_wait();
outb(SLAVE_COMMAND, ICW1_INIT | ICW1_ICW4); // 0x11
io_wait();
```

`ICW1_INIT` (bit 4 = 1) starts the init sequence. `ICW1_ICW4` (bit 0 = 1) tells
the PIC to expect an ICW4. Without it, the PIC stays in 8080/8085 (MCS-80)
compatibility mode, which has different EOI signalling and is incompatible with
x86.

**Step 2 ICW2: "Use these base vectors":**

```c
outb(MASTER_DATA, 0x20);  // IRQ0–IRQ7 → INT 32–39
io_wait();
outb(SLAVE_DATA, 0x28);  // IRQ8–IRQ15 → INT 40–47
io_wait();
```

The base vector must be 8-byte aligned (low 3 bits = 0). After this, IRQ0
triggers vector 32, IRQ1 triggers vector 33, ..., IRQ15 triggers vector 47.
No overlap with CPU exceptions.

**Step 3 ICW3: "Here is how master and slave are connected":**

```c
outb(MASTER_DATA, (1 << 2));  // bit 2 = slave on IRQ2
io_wait();
outb(SLAVE_DATA, 2);     // slave's cascade identity = 2
io_wait();
```

The master receives a bitmap: each bit set to 1 means "a slave PIC is on this
IRQ line." Since the PC architecture wires the slave to master IRQ2, we set bit
2. The slave receives its cascade identity as a plain number (0–7), which must
match the bit position on the master.

**Step 4 ICW4: "We are running on x86, normal EOI mode":**

```c
outb(MASTER_DATA, 0x01);  // ICW4_8086: x86 mode, normal EOI, non-buffered
io_wait();
outb(SLAVE_DATA, 0x01);
io_wait();
```

`ICW4_8086` (bit 0 = 1) selects x86 mode. When this bit is clear, the PIC
emulates the 8080/8085 MCS-80 interrupt protocol, which uses a different EOI
sequence and would not work correctly with our code.

Bits 1 (Auto EOI) and 3–4 (buffered mode) are left at 0 we want normal
(non-auto) EOI so we control the exact moment the interrupt is acknowledged, and
non-buffered mode because our system does not use an external bus buffer
(8289-style).

**Step 5 Restore saved masks:**

```c
outb(MASTER_DATA, saved_master);
outb(SLAVE_DATA, saved_slave);
```

After the init sequence, the PIC resets its IMR to `0xFF` (all masked). We
restore whatever masks were in effect before initialization typically `0xFF` as
well, since we have not unmasked any IRQs yet.

### 5e. End-of-Interrupt (EOI)

After an IRQ handler finishes, it must tell the PIC "I am done, you can send the
next interrupt." This is done by sending an EOI command byte to the command
port:

```c
void pic_send_eoi(uint8_t irq) {
  if (irq >= 8)
    outb(SLAVE_COMMAND, PIC_EOI);  // acknowledge slave first
  outb(MASTER_COMMAND, PIC_EOI);   // then master
}
```

For IRQs 8–15, we must send EOI to both PICs: the slave (which directly
handled the IRQ) and the master (whose IRQ2 was asserted by the slave, and needs
its own in-service bit cleared).

The EOI byte is `0x20` (OCW2 with bits [7:5] = 001 = non-specific EOI). This
clears the highest-priority in-service interrupt bit.

### 5f. Masking and unmasking IRQs

The IMR (Interrupt Mask Register) controls which IRQ lines can send interrupts.
A bit set to 1 means **masked** (blocked). A bit set to 0 means **unmasked**
(allowed):

```c
void irq_mask(uint8_t line) {
  uint16_t port = irq_port(line);    // master or slave DATA port
  uint8_t bit = irq_bit(line);    // which bit (0-7) inside that PIC
  uint8_t imr = inb(port);      // read current mask
  outb(port, imr | (1 << bit));     // set bit → masked
}

void irq_unmask(uint8_t line) {
  uint16_t port = irq_port(line);
  uint8_t bit = irq_bit(line);
  uint8_t imr = inb(port);
  outb(port, imr & ~(1 << bit));    // clear bit → unmasked
}
```

We use a read-modify-write pattern: read the current IMR, modify one bit,
write it back. This preserves the mask state of other IRQ lines. Writing an
IMR byte directly (e.g., `outb(0x21, 0x00)`) would blindly unmask all 8 master
IRQs, including ones the BIOS may have intentionally masked for good reasons.

**Further reading:** <https://wiki.osdev.org/PIC>,
<https://wiki.osdev.org/8259A_PIC>

---

## 6. Interrupt smoke test

### 6a. Proof the chain works

With the IDT loaded and the PIC remapped, we need to verify the interrupt system
end-to-end: CPU takes an interrupt, walks the IDT, runs the stub, calls the C
handler, prints the register dump, and halts.

The simplest test: trigger a software interrupt with the `int $N` instruction:

```c
void kernel_main() {
  pic_remap();
  idt_init();
  vga_init();

  __asm__ volatile ("int $0x3");  // trigger #BP Breakpoint (vector 3)
  for (;;) {}
}
```

Vector 3 (`#BP` Breakpoint) is a good choice for the smoke test because:

1. It is a trap (IF is preserved, so no interrupt-disabling side effects).
2. It is a no-error-code exception, which tests the dummy-error-code path.
3. After `iretq`, the CPU resumes at the instruction **after** the `int $0x3`,
  making it the only exception that is meant to be recoverable by default.

When this code executes, the screen shows:

```
#BP Breakpoint
rax=0x0000000000000000
rbx=0x0000000000000000
...
int_no=0x0000000000000003
err_code=0x0000000000000000
```

Then the kernel halts. The interrupt dispatch pipeline IDT lookup → stub →
C handler → register dump → halt is verified.

### 6b. What about STI/CLI?

The `sti` (Set Interrupt Flag) and `cli` (Clear Interrupt Flag) instructions
enable and disable maskable hardware interrupts. At this milestone:

- `cli` is used in two places: inside `isr_handler` before `hlt` (Section 4e),
 and implicitly by the CPU when it enters an interrupt gate.
- `sti` is not yet used because hardware IRQs are all masked (IMR = `0xFF`).
 Once we enable a hardware IRQ (keyboard, timer), we will call `sti` in
 `kernel_main` after initialization to allow interrupts, and the PIC/IDT
 pipeline will handle them.

Dedicated C wrapper functions (`sti()` / `cli()`) are not yet written they are
trivial one-liners (`__asm__ volatile ("sti")`) and will be added when hardware
IRQ handling begins.

### 6c. Future work

The M2 milestone establishes the interrupt infrastructure. What it does **not**
yet do:

- Handle different exceptions differently (all go to the same panic handler).
- Enable hardware interrupts (all IRQs remain masked).
- Handle IRQs with device-specific handlers (no timer, no keyboard).
- Support user-mode interrupt entry (all gates are DPL 0).
- Support the APIC/LAPIC (modern interrupt controller that supersedes the 8259A).

These are the goals of M3 and beyond.

---

## 7. Initialization order in `kernel_main`

The order of operations in `kernel_main` matters:

```c
void kernel_main() {
  pic_remap();  // FIRST: move IRQs to vectors 32-47 before enabling IDT
  idt_init();   // THEN: load IDT so interrupts are dispatched correctly
  vga_init();   // THEN: prepare display for panic output
  __asm__ volatile ("int $0x3"); // TEST: trigger a breakpoint exception
  for (;;) {}
}
```

**Why PIC remap before IDT init?** If hardware interrupts fire before the PIC
is remapped, they arrive on the default vectors (0x08–0x0F), which are
unregistered in our IDT (only 0–31 are set) and would trigger a `#GP` fault in
the best case or silently corrupt state in the worst case. Remapping first
ensures that any IRQ arriving on vectors 32–47 hits an unregistered slot and
causes a predictable `#GP`.

**Why IDT init before VGA init?** If an exception fires before the IDT is
loaded, the CPU tries to use the BIOS-era IVT (Interrupt Vector Table) at
`0x0000` in real mode but we are in long mode. The result is a triple fault
(reset). Loading the IDT first means any exception that fires during VGA
initialization will be properly dispatched to our panic handler.

---

## 8. Memory map at M2 (updated from M1)

```
0x000000 - 0x000FFF  Real-mode IVT (dead, we are in long mode)
0x007C00 - 0x007DFF  Stage 1 bootsector (dead)
0x008000 - 0x008FFF  Loader code (dead)
0x009000 - 0x009FFF  PML4 table (active, 1 entry)
0x00A000 - 0x00AFFF  PDPT table (active, 1 entry)
0x00B000 - 0x00BFFF  PD table (active, 1 entry 2 MiB huge page)
0x010000 - 0x01FFFF  Kernel ELF buffer (dead)
0x0B8000        VGA text-mode buffer (80×25)
0x100000        Kernel .text, .rodata, .data, .bss
            ├── IDT (256 × 16 bytes = 4 KiB, 16-byte aligned)
            └── isr_stub_table (32 × 8 bytes = 256 bytes)
0x1?????        Stack top (grows down, 16-byte aligned)
```

The IDT and stub table live inside the kernel's `.bss` or `.data` sections at
runtime. The IDT's 16-byte alignment is handled by GCC's `__attribute__((aligned(0x10)))`.

---

## 9. Source files

| File | Purpose | Language |
|------|---------|----------|
| `kernel/arch/x86_64/idt.h` | IDT entry struct, IDTR struct, gate type constants, exception vector defines | C header |
| `kernel/arch/x86_64/idt.c` | `idt_init()` fill IDT entries, load IDTR with `lidt` | C |
| `kernel/arch/x86_64/isr.h` | `registers_t` struct matching stack layout + handler prototype | C header |
| `kernel/arch/x86_64/isr.c` | `isr_handler()` exception name table, register dump, CR2 for `#PF`, halt | C |
| `kernel/arch/x86_64/isr_stubs.asm` | 32 ISR stubs (NASM macros), `isr_common_stub`, stub address table | Assembly (NASM, elf64) |
| `kernel/arch/x86_64/pic.h` | PIC port defines, ICW/OCW constants, function prototypes | C header |
| `kernel/arch/x86_64/pic.c` | `pic_remap()`, `pic_send_eoi()`, `irq_mask()`/`irq_unmask()` | C |
| `kernel/lib/io.h` | `inb()`, `outb()`, `io_wait()` inline assembly wrappers | C header |
| `kernel/lib/stdint.h` | Fixed-width integer types (`uint8_t`, `uint64_t`, etc.) | C header |
| `kernel/lib/stddef.h` | `size_t`, `NULL` | C header |
| `kernel/lib/stdbool.h` | `bool` type, `true`/`false` | C header |
| `kernel/kernel.c` | `kernel_main()` initialization order + smoke test | C |
| `kernel/linker.ld` | Link at 0x100000, export `__bss_start`/`__bss_end` | Linker script |
| `Makefile` | Build: NASM (`isr_stubs.asm` → elf64), GCC `-m64`, LD, disk image | Make |

---

## 10. References

- **IDT:** <https://wiki.osdev.org/IDT>, <https://wiki.osdev.org/Interrupt_Descriptor_Table>
- **ISR design:** <https://wiki.osdev.org/Interrupt_Service_Routines>
- **CPU Exceptions:** <https://wiki.osdev.org/Exceptions>
- **8259A PIC:** <https://wiki.osdev.org/PIC>, <https://wiki.osdev.org/8259A_PIC>
- **Interrupt overview:** <https://wiki.osdev.org/Interrupts>
- **I/O ports:** <https://wiki.osdev.org/I/O_Ports>
- **Inline assembly (GCC):** <https://wiki.osdev.org/Inline_Assembly>
- **Panic / kernel halt:** <https://wiki.osdev.org/Panic>
