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

    /*
     * Permit EL1 to access the GICv3 CPU-interface system registers.
     * ICC_SRE_EL2.SRE (bit 0) and Enable (bit 3) must both be set.
     */
    mov     x0, #0x9
    msr     icc_sre_el2, x0
    isb

    /* EL1h (M[3:0] = 0101), with all DAIF masks set. */
    mov     x0, #0x3c5
    msr     spsr_el2, x0
    adr     x0, .Lel1_entry
    msr     elr_el2, x0
    isb
    eret

.Lel1_entry:
    msr     daifset, #0xf

    /* Allow EL1 Rust/compiler FP+ASIMD use, while trapping EL0 until task FP
     * state save/restore is implemented. CPACR_EL1.FPEN=0b01. */
    movz    x9, #0x10, lsl #16
    msr     cpacr_el1, x9
    isb
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

/*
 * Relocate the live EL1 execution context into the existing TTBR1 direct map.
 * The physical image remains unchanged; only PC, SP, LR and VBAR are rebased.
 * Both aliases remain mapped because the early kernel and current data pointers
 * still use the lower identity window.
 */
.global fahim_relocate_high_half
.type fahim_relocate_high_half, %function
fahim_relocate_high_half:
    movz    x9, #0xffff, lsl #48
    add     x10, sp, #0
    and     x10, x10, x9
    cmp     x10, x9
    b.eq    .Lalready_high_half

    /* Preserve the AAPCS64 callee-saved register set and the caller's LR. */
    stp     x19, x20, [sp, #-96]!
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]
    stp     x27, x28, [sp, #64]
    stp     x29, x30, [sp, #80]

    /* Switch SP to the same physical stack through its TTBR1 alias. */
    add     x10, sp, x9
    mov     sp, x10

    /* Exception vectors must follow the kernel into the upper VA range. */
    adrp    x11, __exception_vectors
    add     x11, x11, :lo12:__exception_vectors
    add     x11, x11, x9
    msr     vbar_el1, x11
    dsb     sy
    ic      iallu
    dsb     sy
    isb

    /* The C caller's return PC also needs the upper direct-map alias. */
    add     x30, x30, x9
    adr     x11, .Lhigh_half_resume
    add     x11, x11, x9
    br      x11

.Lhigh_half_resume:
    ldp     x19, x20, [sp, #0]
    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    ldp     x27, x28, [sp, #64]
    ldp     x29, x30, [sp, #80]
    add     x30, x30, x9
    add     sp, sp, #96
    ret

.Lalready_high_half:
    ret
.size fahim_relocate_high_half, . - fahim_relocate_high_half



/*
 * One-shot EL0 entry trampoline. The current address space is activated by
 * Rust first. The exit SVC handler changes ELR_EL1/SPSR_EL1 to the resume
 * label below, deactivates TTBR0 and returns here with the kernel stack intact.
 */
.global fahim_enter_user
.type fahim_enter_user, %function
fahim_enter_user:
    stp     x19, x20, [sp, #-96]!
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]
    stp     x27, x28, [sp, #64]
    stp     x29, x30, [sp, #80]

    mov     x19, x0                  /* user entry virtual address */
    mov     x20, x1                  /* user stack top */
    adr     x0, .Luser_return_resume
    bl      fahim_address_space_set_user_return_pc
    cbnz    w0, .Luser_enter_failed

    msr     sp_el0, x20
    msr     elr_el1, x19
    movz    x9, #0x03c0              /* EL0t, all DAIF masks set */
    msr     spsr_el1, x9
    isb
    eret

.Luser_enter_failed:
    ldp     x19, x20, [sp, #0]
    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    ldp     x27, x28, [sp, #64]
    ldp     x29, x30, [sp, #80]
    add     sp, sp, #96
    ret

.Luser_return_resume:
    ldp     x19, x20, [sp, #0]
    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    ldp     x27, x28, [sp, #64]
    ldp     x29, x30, [sp, #80]
    add     sp, sp, #96
    ret
.size fahim_enter_user, . - fahim_enter_user

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

/*
 * Trap frame: x0-x30 plus ELR_EL1, SPSR_EL1 and SP_EL0.
 * Keeping the system return state on each task's kernel stack is essential:
 * a scheduler switch may suspend one EL0 exception and resume another later.
 */
.macro EXCEPTION_STUB number
.Lexception_vector_\number:
    msr     daifset, #0xf
    sub     sp, sp, #288
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

    mrs     x9, elr_el1
    str     x9, [sp, #248]
    mrs     x9, spsr_el1
    str     x9, [sp, #256]
    mrs     x9, sp_el0
    str     x9, [sp, #264]

.if \number == 5 || \number == 9
    /* Current-EL and lower-EL IRQs share the GIC/timer dispatch path. */
    bl      irq_handle
.elseif \number == 8
    /* Lower-EL AArch64 synchronous entry (SVC/syscall dispatch). */
    mov     x0, sp
    bl      user_sync_handle
.else
    mov     x0, sp
    mov     x1, #\number
    bl      exception_handle
.Lexception_stub_halt_\number:
    wfe
    b       .Lexception_stub_halt_\number
.endif

    /* Restore the return state belonging to this saved task frame. */
    ldr     x9, [sp, #248]
    msr     elr_el1, x9
    ldr     x9, [sp, #256]
    msr     spsr_el1, x9
    ldr     x9, [sp, #264]
    msr     sp_el0, x9

    ldp     x0,  x1,  [sp, #0]
    ldp     x2,  x3,  [sp, #16]
    ldp     x4,  x5,  [sp, #32]
    ldp     x6,  x7,  [sp, #48]
    ldp     x8,  x9,  [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    add     sp, sp, #288
    eret
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


/* Tiny EL0 smoke payload: return a recognisable value through SVC #0xF02. */
.section .rodata.user_test, "a"
.balign 4
.global __user_smoke_code_start
.global __user_smoke_code_end
__user_smoke_code_start:
    movz    x0, #0xf00d
    svc     #0xf02
.Luser_smoke_wait:
    b       .Luser_smoke_wait
__user_smoke_code_end:

/* Two-process EL0 payload for the timer-preemption integration test.
 * SYS_GETPID=3, SYS_LOG=4, SYS_EXIT=1, SVC immediate=0.
 * Both processes share one RW page at 0x100600000. The first process spins
 * until the second increments the shared word. Without a timer preemption,
 * the first process can never make progress past this wait loop.
 */
.balign 4
.global __user_process_code_start
.global __user_process_code_end
__user_process_code_start:
    /* Read a message from the RO+X image using checked copy_from_user. */
    adr     x0, .Luser_message
    movz    x1, #5
    movz    x8, #7
    svc     #0                  /* SYS_WRITE("Fahim") */
    cmp     x0, #5
    b.ne    .Lwaitpid_failed

    /* A kernel-half pointer must be rejected without dereferencing it. */
    movz    x0, #0xffff, lsl #48
    movz    x1, #5
    movz    x8, #7
    svc     #0
    movn    x2, #13              /* -EFAULT */
    cmp     x0, x2
    b.ne    .Lwaitpid_failed

    movz    x8, #3
    svc     #0                  /* x0 = process id */
    movz    x1, #0xa
    movz    x8, #4
    svc     #0                  /* marker A: task is running */

    movz    x10, #0x60, lsl #16
    movk    x10, #0x1, lsl #32  /* shared word at 0x100600000 */
    ldr     x5, [x10]
    add     x5, x5, #1
    str     x5, [x10]

.Lwait_for_peers:
    ldr     x5, [x10]
    cmp     x5, #3
    b.lo    .Lwait_for_peers    /* timer preemption must run all three normal tasks */

    movz    x8, #3
    svc     #0                  /* x0 = process id */
    cmp     x0, #101
    b.eq    .Lparent_wait
    cmp     x0, #202
    b.eq    .Lchild_sleep

    /* PID 404: independent root task with a 50 ms sleep deadline. */
    movz    x1, #0xc
    movz    x8, #4
    svc     #0                  /* marker C: entering sleep */
    movz    x0, #50
    movz    x8, #5
    svc     #0                  /* SYS_SLEEP(50) */
    movz    x8, #3
    svc     #0
    movz    x1, #0xb
    movz    x8, #4
    svc     #0                  /* marker B: resumed after wakeup */
    movz    x0, #0x44
    movz    x8, #1
    svc     #0                  /* exit 68 */

.Lchild_sleep:
    movz    x1, #0xc
    movz    x8, #4
    svc     #0                  /* marker C: entering sleep */
    movz    x0, #30
    movz    x8, #5
    svc     #0                  /* SYS_SLEEP(30) */
    movz    x0, #42
    movz    x8, #1
    svc     #0                  /* child exits with status 42 */

.Lparent_wait:
    /* A read-only code page is not a valid wait status destination. */
    movz    x0, #202
    movz    x1, #0x20, lsl #16
    movk    x1, #0x1, lsl #32  /* 0x100200000: RO+X code page */
    movz    x8, #6
    svc     #0
    movn    x2, #13              /* -EFAULT */
    cmp     x0, x2
    b.ne    .Lwaitpid_failed

    /* Retry with a mapped RW+XN status word in the parent's shared data page. */
    movz    x0, #202
    movz    x1, #0x60, lsl #16
    movk    x1, #0x1, lsl #32
    add     x1, x1, #8           /* shared_user_va + sizeof(uint64_t) */
    movz    x8, #6
    svc     #0                  /* SYS_WAITPID; x0=pid, status copied to x1 ptr */
    cmp     x0, #202
    b.ne    .Lwaitpid_failed

    ldr     w1, [x10, #8]
    movz    w2, #0x2a           /* POSIX normal-exit status 42 << 8 */
    lsl     w2, w2, #8
    cmp     w1, w2
    b.ne    .Lwaitpid_failed

    movz    x8, #3
    svc     #0
    movz    x1, #0xd
    movz    x8, #4
    svc     #0                  /* marker D: child status copied and reaped */
    movz    x0, #0
    movz    x8, #1
    svc     #0                  /* parent exits */

.Lwaitpid_failed:
    wfe
    b       .Lwaitpid_failed
.Luser_process_halt:
    wfe
    b       .Luser_process_halt
.Luser_message:
    .ascii  "Fahim"
    .balign 4
__user_process_code_end:

/* Fault-isolation test process: attempt to write to its own RO+X code page. */
.balign 4
.global __user_fault_code_start
.global __user_fault_code_end
__user_fault_code_start:
    movz    x8, #3
    svc     #0                  /* x0 = process id */
    movz    x1, #0xf
    movz    x8, #4
    svc     #0                  /* log fault-test marker */
    movz    x10, #0x20, lsl #16
    movk    x10, #0x1, lsl #32  /* 0x100200000: read-only executable page */
    str     xzr, [x10]          /* EL0 permission fault; process must be killed */
.Lfault_process_halt:
    wfe
    b       .Lfault_process_halt
__user_fault_code_end:

/* Embedded static user ELF64 fixture built by the AArch64 cross toolchain. */
#ifndef FAHIM_USER_ELF_PATH
#error "FAHIM_USER_ELF_PATH must name the generated user ELF"
#endif
.section .rodata.user_elf, "a"
.balign 8
.global __fahim_user_elf_image_start
.global __fahim_user_elf_image_end
__fahim_user_elf_image_start:
    .incbin FAHIM_USER_ELF_PATH
__fahim_user_elf_image_end:

.section .bss.stack, "aw", %nobits
.align 12
.global __stack_bottom
__stack_bottom:
    .skip 65536
.global __stack_top
__stack_top:
