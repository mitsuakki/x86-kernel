; Stage 1 - 512-byte bootsector.
; BIOS firmware loads the first sector of the boot disk to 0x7C00
; and executes it in 16-bit real mode (CS=0, IP=0x7C00).
;
; Our job: load the loader (stage 2) from the sectors right after us
; and hand off control.  We use BIOS Extended Read (INT 0x13 AH=0x42)
; because it takes an LBA address.  Simpler than CHS math.
;
; Disk layout:
;   LBA 0       -> boot.asm   (this sector)
;   LBA 1-8     -> loader.asm (stage 2, up to 4 KiB)
;   LBA 9+      -> kernel.elf

[ORG 0x7C00]
[BITS 16]

start:
    ; Zero out segment registers
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [drive_num], dl        ; BIOS drive number

    ; Load loader from LBA 1 using BIOS Extended Read
    mov si, dap
    mov ah, 0x42
    mov dl, [drive_num]
    int 0x13
    jc  disk_err

    jmp 0x0000:0x8000

disk_err:
    mov al, '1'
    mov ah, 0x0E
    int 0x10
    jmp $

drive_num: db 0

; DAP (Disk Address Packet) — passed to INT 0x13 AH=0x42.
; Format: [size(1) reserved(1) count(2) offset(2) segment(2) LBA(8)]
dap:
    db 0x10                    ; packet size (16 bytes)
    db 0                       ; reserved, must be 0
    dw 8                       ; sector count — 8 × 512 = 4 KiB for loader
    dw 0x8000                  ; buffer offset  (physical = seg:off = 0x0000:0x8000)
    dw 0x0000                  ; buffer segment
    dq 1                       ; starting LBA (sector 1, right after us)

; Pad to 510 bytes, then write the boot signature.
; BIOS requires bytes 511-512 to be 0x55 0xAA to recognize this as bootable.
times 510 - ($ - $$) db 0
dw 0xAA55
