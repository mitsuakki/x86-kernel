[ORG 0x7C00]
[BITS 16]

start:
    ; Zero out segment registers
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Save drive number passed by BIOS in DL
    mov [drive_number], dl

    ; Load kernel from disk (sectors 2-5) to physical address 0x1000
    mov ah, 0x02       ; BIOS: read sectors
    mov al, 0x04       ; sector count
    mov ch, 0x00       ; cylinder 0
    mov cl, 0x02       ; start at sector 2
    mov dh, 0x00       ; head 0
    mov dl, [drive_number]
    mov bx, 0x0100
    mov es, bx
    xor bx, bx         ; ES:BX = 0x100:0x0000 = physical 0x1000
    int 0x13
    jc disk_error

    ; Enable A20 line using FAST A20 Gate (unmask addresses above 1MB)
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Disable interrupts 
    ; BIOS interrupts are invalid in protected mode
    cli

    ; Load GDT
    lgdt [gdt_descriptor]

    ; Set PE bit in CR0 to enter protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump flushes the prefetch queue and reloads CS from GDT
    jmp 0x08:protected_mode_entry

disk_error:
    mov ah, 0x0E
    mov al, 'E'
    int 0x10
    jmp $

drive_number: db 0

%include "boot/gdt.asm"

[BITS 32]
protected_mode_entry:
    ; Load data segment selector (0x10) into all segment registers
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Set up stack at 0x90000
    mov esp, 0x90000
    mov ebp, esp

    ; Write 'A' to VGA text buffer as proof of life
    mov byte [0xB8000], 'A'
    mov byte [0xB8001], 0x0F   ; black background, white foreground

    ; Jump to kernel (loaded at 0x1000, linked with -Ttext 0x1000)
    call 0x1000

    ; Kernel should never return — loop forever just in case
    jmp $

; Boot sector signature
times 510 - ($ - $$) db 0x00
dw 0xAA55
