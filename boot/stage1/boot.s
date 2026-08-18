; Stage 1 - 512-byte bootsector.
; BIOS firmware loads the first sector of the boot disk to 0x7C00
; and executes it in 16-bit real mode (CS=0, IP=0x7C00).
;
; Our job: load the loader (stage 2) from the sectors right after us
; and hand off control.  We use BIOS CHS Read (INT 0x13 AH=0x02).
;
; Disk layout:
;   LBA 0       -> boot.s     (this sector)
;   LBA 1-8     -> loader.s   (stage 2, up to 4 KiB)
;   LBA 9+      -> kernel.elf

[ORG 0x7C00]                 ; BIOS loads us at 0x7C00: offsets must match
[BITS 16]                    ; real mode = 16-bit code

start:
    ; Zero out segment registers - we address everything as 0x0000:offset
    xor ax, ax               ; ax = 0
    mov ds, ax               ; data segment = 0
    mov es, ax               ; extra segment = 0
    mov ss, ax               ; stack segment = 0
    mov sp, 0x7C00           ; stack grows down from 0x7C00 (below our code)

    mov [drive_num], dl      ; save BIOS drive number for INT 0x13

    ; Load loader from LBA 1 using CHS (INT 0x13 AH=0x02)
    mov ah, 0x02             ; BIOS read sectors function
    mov al, 8                ; sector count = 8 (4 KiB for stage 2)
    mov ch, 0                ; cylinder 0
    mov cl, 2                ; sector 2 = LBA 1 (CHS: cyl 0, head 0, sect 2)
    mov dh, 0                ; head 0
    mov dl, [drive_num]      ; which drive to read
    mov bx, 0x8000           ; buffer: ES:BX = 0x0000:0x8000 = physical 0x8000
    int 0x13                 ; BIOS disk service: read sectors
    jc  disk_err             ; carry set = read failed

    jmp 0x0000:0x8000        ; far jump to stage 2 (CS=0, IP=0x8000)

disk_err:
    mov al, '1'              ; '1' = stage-1 disk error
    mov ah, 0x0E             ; BIOS teletype output
    int 0x10                 ; BIOS video service: print char in AL
    jmp $                    ; hang forever

drive_num: db 0              ; drive number saved at boot

; DAP (Disk Address Packet) - the input format for INT 0x13 AH=0x42
; (Extended Read).  Not used: we read via CHS (AH=0x02) instead.
; Format: [size(1) reserved(1) count(2) offset(2) segment(2) LBA(8)]
dap:
    db 0x10                   ; packet size (16 bytes)
    db 0                      ; reserved, must be 0
    dw 8                      ; sector count - 8 x 512 = 4 KiB for loader
    dw 0x8000                 ; buffer offset  (physical = seg:off = 0x0000:0x8000)
    dw 0x0000                 ; buffer segment
    dq 1                      ; starting LBA (sector 1, right after us)

; Pad to 510 bytes, then write the boot signature.
; BIOS requires bytes 511-512 to be 0x55 0xAA to recognize this as bootable.
times 510 - ($ - $$) db 0     ; pad with zeros up to byte 510
dw 0xAA55                     ; boot signature (bytes 511-512)
