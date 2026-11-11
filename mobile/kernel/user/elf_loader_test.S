/* Tiny static AArch64 ELF64 test program. */
.section .text.start, "ax"
.balign 4
.global _start
.type _start, %function
_start:
    /* Read a string from the ELF's separate RO+XN PT_LOAD segment. */
    adrp    x0, .Lmessage
    add     x0, x0, :lo12:.Lmessage
    movz    x1, #9
    movz    x8, #7              /* SYS_WRITE */
    svc     #0
    cmp     x0, #9
    b.ne    .Lfailed

    /* Validate initialized data from the RW+XN PT_LOAD segment. */
    adrp    x9, .Linitialized_data
    add     x9, x9, :lo12:.Linitialized_data
    ldr     w10, [x9]
    movz    w11, #0x1234
    cmp     w10, w11
    b.ne    .Lfailed

    /* The loader must zero BSS bytes not present in the ELF file. */
    adrp    x9, .Lbss_zero
    add     x9, x9, :lo12:.Lbss_zero
    ldr     x10, [x9]
    cbnz    x10, .Lfailed
    movz    w10, #0xabcd
    str     w10, [x9]

    movz    x8, #3              /* SYS_GETPID */
    svc     #0
    movz    x1, #0xee
    movz    x8, #4              /* SYS_LOG */
    svc     #0

    movz    x0, #0x55
    movz    x8, #1              /* SYS_EXIT */
    svc     #0

.Lfailed:
    movz    x0, #0xff
    movz    x8, #1
    svc     #0

.Lhang:
    wfe
    b       .Lhang
.size _start, . - _start

.section .rodata, "a"
.balign 4
.Lmessage:
    .ascii "ELF ready"

.section .data, "aw"
.balign 4
.Linitialized_data:
    .word 0x1234

.section .bss, "aw", %nobits
.balign 8
.Lbss_zero:
    .skip 8
