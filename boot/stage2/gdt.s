; GDT (Global Descriptor Table) - flat memory model, 32-bit + 64-bit.
;
; Each descriptor is 8 bytes.  We need four entries:
;   0. Null descriptor (CPU requirement, catches null selectors)
;   1. Code segment (selector 0x08) - ring 0, 32-bit, executable, readable
;   2. Data segment (selector 0x10) - ring 0, writable (works in 32-bit and long mode)
;   3. Code segment (selector 0x18) - ring 0, 64-bit long mode
;
; Descriptor layout (64 bits):
;   bits 63:56 = base[31:24]
;   bits 55:52 = flags (4 bits: Gr Sz L 0)
;   bits 51:48 = limit[19:16]
;   bits 47:40 = access byte (8 bits)
;   bits 39:16 = base[23:0]
;   bits 15:0  = limit[15:0]
;
; Access byte (bits 47:40):
;   bit 7 = Present (1 = valid descriptor)
;   bit 6-5 = DPL (Descriptor Privilege Level, 0 = ring 0)
;   bit 4 = S (1 = code/data segment, 0 = system)
;   bit 3 = Type.E (1 = executable / 0 = data)
;   bit 2 = Type.DC (data: direction/expand; code: conforming)
;   bit 1 = Type.RW (data: writable; code: readable)
;   bit 0 = Type.A (accessed, CPU sets this)
;
; Flags (bits 55:52):
;   bit 3 = Gr (Granularity: 0 = byte-granular limit, 1 = 4 KiB-granular)
;   bit 2 = Sz (Size: 0 = 16-bit prot, 1 = 32-bit prot; for code, ignored when L=1)
;   bit 1 = L  (Long mode: 1 = 64-bit code segment)
;   bit 0 = 0  (reserved)
;
; Flat model: base=0, limit=0xFFFFF (4 GiB with Gr=1).

gdt_start:

; ---- Null descriptor (selector 0x00) ----
gdt_null:
    dd 0x0
    dd 0x0

; ---- Code segment (selector 0x08) ----
gdt_code:
    dw 0xFFFF       ; limit[15:0] = 0xFFFF
    dw 0x0000       ; base[15:0]  = 0
    db 0x00         ; base[23:16] = 0
    db 10011010b    ; access: Present, DPL=0, S=1, Executable, Readable, A=0
    db 11001111b    ; flags: Gr=1 (4K), Sz=1 (32-bit); limit[19:16] = 0xF
    db 0x00         ; base[31:24] = 0

; ---- Data segment (selector 0x10) ----
gdt_data:
    dw 0xFFFF       ; limit[15:0] = 0xFFFF
    dw 0x0000       ; base[15:0]  = 0
    db 0x00         ; base[23:16] = 0
    db 10010010b    ; access: Present, DPL=0, S=1, Data(!Exec), Writable, A=0
    db 11001111b    ; flags: Gr=1 (4K), Sz=1 (32-bit); limit[19:16] = 0xF
    db 0x00         ; base[31:24] = 0

; ---- 64-bit Code segment (selector 0x18) ----
; L=1 tells the CPU this is a 64-bit code segment.  Sz=0 (ignored when L=1).
; Limit of 0xFFFFF (4 GiB with Gr=1) satisfies the last limit check on the far jump.
gdt_code64:
    dw 0xFFFF       ; limit[15:0]
    dw 0x0000       ; base[15:0]
    db 0x00         ; base[23:16]
    db 10011010b    ; access: Present, DPL=0, S=1, Executable, Readable
    db 10101111b    ; flags: Gr=1 (4K), Sz=0, L=1 (long mode); limit[19:16] = 0xF
    db 0x00         ; base[31:24]

gdt_end:

; GDTR value passed to lgdt: [limit (2 bytes)] [base (4 bytes)]
gdt_descriptor:
    dw gdt_end - gdt_start - 1    ; limit = size of GDT minus 1
    dd gdt_start                  ; linear address of the GDT
