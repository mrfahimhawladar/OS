ENTRY(_start)

SECTIONS
{
    . = 1M;

    .multiboot :
    {
        KEEP(*(.multiboot))
    }

    . = ALIGN(4K);
    .text :
    {
        *(.text*)
    }

    . = ALIGN(4K);
    .rodata :
    {
        *(.rodata*)
    }

    . = ALIGN(4K);
    .data :
    {
        *(.data*)
    }

    . = ALIGN(4K);
    .bss :
    {
        *(COMMON)
        *(.bss*)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.got*)
        *(.iplt*)
    }
}
