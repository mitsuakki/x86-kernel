mov ah, 0x0e
mov al, 65 ; starting with capital A
int 0x10

; print the alphabet while alternating caps
.loop:
    cmp al, 120 ; 122 is for z
    jg .done

    xor al, 32 ; the space for each capital/lowercase is 32
    inc al

    int 0x10 
    jmp .loop


.done:
    jmp $ ; jump to current address forever

; Boot sector padding
times 510 - ($ - $$) db 0x0
dw 0xaa55