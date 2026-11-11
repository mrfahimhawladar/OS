ENTRY(_start)

PHDRS
{
    text   PT_LOAD FILEHDR PHDRS FLAGS(5); /* R + X */
    rodata PT_LOAD FLAGS(4);               /* R + XN */
    data   PT_LOAD FLAGS(6);               /* R + W + XN */
}

SECTIONS
{
    /* The ELF header and program-header table share the first executable load. */
    . = 0x100200000 + SIZEOF_HEADERS;

    .text :
    {
        *(.text.start)
        *(.text .text.*)
        . = ALIGN(4);
    } :text

    . = ALIGN(0x1000);
    .rodata :
    {
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } :rodata

    . = ALIGN(0x1000);
    .data :
    {
        *(.data .data.*)
    } :data

    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
