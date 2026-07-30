ENTRY(kernel_main)

SECTIONS
{
    . = 0x100000;           /* Kernel loads at 1 MiB */

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
