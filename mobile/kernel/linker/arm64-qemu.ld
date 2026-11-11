ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5); /* R + X */
    rodata PT_LOAD FLAGS(4); /* R */
    data   PT_LOAD FLAGS(6); /* R + W */
}

SECTIONS
{
    /* QEMU virt places its bare-metal DTB at 0x40000000. */
    . = 0x40200000;
    __kernel_start = .;

    .text :
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } :text

    . = ALIGN(0x10000);
    .rodata :
    {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(0x10000);
    .data :
    {
        *(.data .data.*)
    } :data

    .bss (NOLOAD) :
    {
        . = ALIGN(4K);
        __bss_start = .;
        *(.bss.stack)
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    . = ALIGN(4K);
    __kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
