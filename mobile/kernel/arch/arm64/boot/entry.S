/*
 * Fahim OS ARM64 bare-metal entry for the QEMU virt reference platform.
 * No stack is used until BSS is cleared because the bootstrap stack is in BSS.
 */
.section .text.boot, "ax"
.align 7
.global _start
.type _start, %function

_start:
    msr     daifset, #0xf

    /* Initialize zero-state data before any C code or stack use. */
    adrp    x9, __bss_start
    add     x9, x9, :lo12:__bss_start
    adrp    x10, __bss_end
    add     x10, x10, :lo12:__bss_end
.Lzero_bss:
    cmp     x9, x10
    b.hs    .Lbss_done
    sub     x11, x10, x9
    cmp     x11, #8
    b.lo    .Lbss_tail
    str     xzr, [x9], #8
    b       .Lzero_bss
.Lbss_tail:
    cmp     x9, x10
    b.hs    .Lbss_done
    strb    wzr, [x9], #1
    b       .Lbss_tail
.Lbss_done:

    /* Normalize supported firmware entry levels to EL1h. */
    mrs     x9, CurrentEL
    cmp     x9, #0x8
    b.eq    .Lfrom_el2
    cmp     x9, #0x4
    b.eq    .Lel1_entry

.Lunsupported_el:
    wfe
    b       .Lunsupported_el

.Lfrom_el2:
    msr     spsel, #1
    ldr     x1, =__stack_top
    mov     sp, x1
    msr     sp_el1, x1

    /* EL1 executes AArch64; expose the physical counter/timer interface. */
    mov     x0, #0x80000000
    msr     hcr_el2, x0
    mov     x0, #0x3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    /* EL1h (M[3:0] = 0101), with all DAIF masks set. */
    mov     x0, #0x3c5
    msr     spsr_el2, x0
    adr     x0, .Lel1_entry
    msr     elr_el2, x0
    isb
    eret

.Lel1_entry:
    msr     daifset, #0xf
    msr     spsel, #1
    ldr     x1, =__stack_top
    mov     sp, x1

    /* Install the aligned EL1 vector table before entering C. */
    adrp    x0, __exception_vectors
    add     x0, x0, :lo12:__exception_vectors
    msr     vbar_el1, x0
    dsb     sy
    isb

    /* QEMU virt bare-metal DTB location. */
    ldr     x0, =0x40000000
    bl      kernel_main

.Lkernel_halt:
    wfe
    b       .Lkernel_halt

.size _start, . - _start
.ltorg

.section .text.vectors, "ax"
.balign 2048
.global __exception_vectors
.type __exception_vectors, %function
__exception_vectors:
.macro VECTOR_ENTRY target
    b       \target
    .space  124
.endm
    /* Current EL using SP_EL0: Sync, IRQ, FIQ, SError. */
    VECTOR_ENTRY .Lexception_vector_0
    VECTOR_ENTRY .Lexception_vector_1
    VECTOR_ENTRY .Lexception_vector_2
    VECTOR_ENTRY .Lexception_vector_3
    /* Current EL using SP_EL1: Sync, IRQ, FIQ, SError. */
    VECTOR_ENTRY .Lexception_vector_4
    VECTOR_ENTRY .Lexception_vector_5
    VECTOR_ENTRY .Lexception_vector_6
    VECTOR_ENTRY .Lexception_vector_7
    /* Lower EL AArch64: Sync, IRQ, FIQ, SError. */
    VECTOR_ENTRY .Lexception_vector_8
    VECTOR_ENTRY .Lexception_vector_9
    VECTOR_ENTRY .Lexception_vector_10
    VECTOR_ENTRY .Lexception_vector_11
    /* Lower EL AArch32: Sync, IRQ, FIQ, SError. */
    VECTOR_ENTRY .Lexception_vector_12
    VECTOR_ENTRY .Lexception_vector_13
    VECTOR_ENTRY .Lexception_vector_14
    VECTOR_ENTRY .Lexception_vector_15
.size __exception_vectors, . - __exception_vectors

/* Save x0-x30 in a 16-byte-aligned 256-byte frame before calling C. */
.macro EXCEPTION_STUB number
.Lexception_vector_\number:
    msr     daifset, #0xf
    sub     sp, sp, #256
    stp     x0,  x1,  [sp, #0]
    stp     x2,  x3,  [sp, #16]
    stp     x4,  x5,  [sp, #32]
    stp     x6,  x7,  [sp, #48]
    stp     x8,  x9,  [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]
    mov     x0, sp
    mov     x1, #\number
    bl      exception_handle
.Lexception_stub_halt_\number:
    wfe
    b       .Lexception_stub_halt_\number
.endm

EXCEPTION_STUB 0
EXCEPTION_STUB 1
EXCEPTION_STUB 2
EXCEPTION_STUB 3
EXCEPTION_STUB 4
EXCEPTION_STUB 5
EXCEPTION_STUB 6
EXCEPTION_STUB 7
EXCEPTION_STUB 8
EXCEPTION_STUB 9
EXCEPTION_STUB 10
EXCEPTION_STUB 11
EXCEPTION_STUB 12
EXCEPTION_STUB 13
EXCEPTION_STUB 14
EXCEPTION_STUB 15

.section .bss.stack, "aw", %nobits
.align 12
.global __stack_bottom
__stack_bottom:
    .skip 65536
.global __stack_top
__stack_top:
