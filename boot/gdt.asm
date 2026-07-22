; 32-bit GDT: null, code, data — flat memory model, ring 0.

gdt_start:

; Entry 0: null descriptor (required by CPU)
gdt_null:
    dd 0x0
    dd 0x0

; Entry 1: 32-bit code segment (selector 0x08)
gdt_code:
    dw 0xFFFF       ; limit (bits 0-15)
    dw 0x0000       ; base (bits 0-15)
    db 0x00         ; base (bits 16-23)
    db 10011010b    ; access: Present, Ring 0, Executable, Readable
    db 11001111b    ; flags: 4KB granularity, 32-bit + limit (bits 16-19)
    db 0x00         ; base (bits 24-31)

; Entry 2: 32-bit data segment (selector 0x10)
gdt_data:
    dw 0xFFFF       ; limit (bits 0-15)
    dw 0x0000       ; base (bits 0-15)
    db 0x00         ; base (bits 16-23)
    db 10010010b    ; access: Present, Ring 0, Writable
    db 11001111b    ; flags: 4KB granularity, 32-bit + limit (bits 16-19)
    db 0x00         ; base (bits 24-31)

gdt_end:

; Descriptor passed to lgdt: [size (2 bytes)] [base (4 bytes)]
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
