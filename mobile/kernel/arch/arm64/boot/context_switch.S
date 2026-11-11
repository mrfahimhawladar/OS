/*
 * AArch64 callee-saved context switch, adapted from KNCn23/mini-rtos
 * (kernel/context_switch.S). Fahim OS compiles the kernel with
 * -mgeneral-regs-only, so no SIMD/FP callee-saved state is live here.
 *
 * void fahim_context_switch(Context *previous, const Context *next)
 */
.section .text
.align 4
.global fahim_context_switch
.type fahim_context_switch, %function
fahim_context_switch:
    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]
    stp     x29, x30, [x0, #80]
    mov     x9, sp
    str     x9, [x0, #96]

    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]
    ldp     x29, x30, [x1, #80]
    ldr     x9, [x1, #96]
    mov     sp, x9
    ret
.size fahim_context_switch, . - fahim_context_switch

/*
 * Initial user-task context. x19=entry VA, x20=user stack top, x21=PID.
 * The scheduler has already installed the task's TTBR0 mapping and switched
 * to its private EL1 kernel stack before this trampoline executes.
 */
.global fahim_user_task_trampoline
.type fahim_user_task_trampoline, %function
fahim_user_task_trampoline:
    mov     x0, x21
    msr     sp_el0, x20
    msr     elr_el1, x19
    msr     spsr_el1, xzr       /* EL0t, IRQ/FIQ unmasked */
    isb
    eret
.size fahim_user_task_trampoline, . - fahim_user_task_trampoline
