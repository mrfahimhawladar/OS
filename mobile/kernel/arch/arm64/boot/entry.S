/*
 * Fahim OS ARM64 bare-metal entry for the QEMU virt reference platform.
 *
 * QEMU's bare-metal virt boot places its generated DTB at the start of RAM.
 * The first kernel milestone reads it from 0x40000000.
 */
.section .text.boot, "ax"
.align 7
.global _start
.type _start, %function

_start:
    /* Mask IRQ/FIQ while the first-stage kernel is initializing. */
    msr     daifset, #0xf

    /* Establish a private, 16-byte-aligned bootstrap stack. */
    ldr     x1, =__stack_top
    mov     sp, x1

    /* QEMU virt bare-metal DTB location. */
    ldr     x0, =0x40000000
    bl      kernel_main

1:
    wfe
    b       1b

.size _start, . - _start

.section .bss.stack, "aw", %nobits
.align 12
.global __stack_bottom
__stack_bottom:
    .skip 65536
.global __stack_top
__stack_top:
