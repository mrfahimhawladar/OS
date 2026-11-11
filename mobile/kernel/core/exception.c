#include <stdint.h>

#include "exception.h"
#include "gicv3_driver.h"
#include "generic_timer.h"
#include "address_space.h"
#include "scheduler.h"
#include "syscall.h"

void console_write(const char *text);
void console_putc(char character);

static volatile uint32_t exception_active;

static void exception_halt(void) __attribute__((noreturn));

static void exception_halt(void) {
    for (;;) {
        __asm__ volatile("msr daifset, #0xf\n\twfe" ::: "memory");
    }
}

static void write_hex64(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    console_write("0x");

    for (int shift = 60; shift >= 0; shift -= 4) {
        console_putc(digits[(value >> (uint32_t)shift) & 0x0fu]);
    }
}

/*
 * Current EL SPx IRQ entry. The upstream GIC API performs IAR acknowledge
 * and EOIR completion. The timer is re-armed before automatic EOI so its
 * level-sensitive signal is deasserted first.
 */
void irq_handle(void) {
    const uint32_t intid = fahim_gicv3_dispatch_irq();

    if (intid == UINT32_MAX) {
        /* GIC spurious/no-pending interrupt: nothing to deactivate. */
        return;
    }

    if (intid == 30u) {
        if (fahim_scheduler_is_active()) {
            /* Keep the preemption path quiet; the timer is re-armed upstream. */
            fahim_scheduler_tick();
        } else {
            console_write("IRQ Dispatcher: handled timer PPI 30, tick ");
            write_hex64(fahim_generic_timer_irq_count());
            console_write("\n");
        }
        return;
    }

    console_write("IRQ Dispatcher: unexpected interrupt INTID ");
    write_hex64(intid);
    console_write("\n");
    exception_halt();
}

static const char *vector_name(uint64_t vector) {
    static const char *const names[16] = {
        "current EL SP0 synchronous",
        "current EL SP0 IRQ",
        "current EL SP0 FIQ",
        "current EL SP0 SError",
        "current EL SPx synchronous",
        "current EL SPx IRQ",
        "current EL SPx FIQ",
        "current EL SPx SError",
        "lower EL AArch64 synchronous",
        "lower EL AArch64 IRQ",
        "lower EL AArch64 FIQ",
        "lower EL AArch64 SError",
        "lower EL AArch32 synchronous",
        "lower EL AArch32 IRQ",
        "lower EL AArch32 FIQ",
        "lower EL AArch32 SError"
    };

    return vector < 16u ? names[vector] : "invalid vector index";
}

void exception_handle(const uint64_t *registers, uint64_t vector) {
    /* Prevent recursive diagnostics if the reporting path itself faults. */
    if (exception_active != 0u) {
        exception_halt();
    }
    exception_active = 1u;

    uint64_t current_el;
    uint64_t esr;
    uint64_t elr;
    uint64_t far;
    uint64_t spsr;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
    __asm__ volatile("mrs %0, ESR_EL1" : "=r"(esr));
    __asm__ volatile("mrs %0, ELR_EL1" : "=r"(elr));
    __asm__ volatile("mrs %0, FAR_EL1" : "=r"(far));
    __asm__ volatile("mrs %0, SPSR_EL1" : "=r"(spsr));

    console_write("\n*** FAHIM OS EXCEPTION ***\n");
    console_write("Exception vector index: ");
    write_hex64(vector);
    console_write("\nException vector: ");
    console_write(vector_name(vector));
    console_write("\nCurrentEL: ");
    write_hex64(current_el);
    console_write("\nESR_EL1: ");
    write_hex64(esr);
    console_write("\nELR_EL1: ");
    write_hex64(elr);
    console_write("\nFAR_EL1: ");
    write_hex64(far);
    console_write("\nSPSR_EL1: ");
    write_hex64(spsr);
    console_write("\nSaved x0: ");
    write_hex64(registers[0]);
    console_write("\nSaved x30: ");
    write_hex64(registers[30]);
    console_write("\n");

#ifdef FAHIM_EXCEPTION_SELFTEST
    const uint64_t exception_class = (esr >> 26) & 0x3fu;
    const uint64_t instruction_length = (esr >> 25) & 0x1u;
    const uint64_t immediate = esr & 0xffffu;

    if (vector == 4u && exception_class == 0x3cu &&
        instruction_length == 1u && immediate == 0x0f05u) {
        console_write("Exception self-test: PASS (BRK #0xF05)\n");
    } else {
        console_write("Exception self-test: FAIL (unexpected syndrome)\n");
    }
#endif

    console_write("Fatal exception: CPU halted.\n");
    exception_halt();
}


/*
 * Lower-EL AArch64 synchronous entry. Each trap frame belongs to the
 * interrupted task's EL1 kernel stack, so a scheduler switch may suspend this
 * handler and resume it later without losing ELR/SPSR/SP_EL0.
 */
void user_sync_handle(uint64_t *registers) {
    if (registers == NULL) {
        exception_halt();
    }

    uint64_t esr;
    __asm__ volatile("mrs %0, ESR_EL1" : "=r"(esr));
    const uint64_t exception_class = (esr >> 26) & 0x3fu;
    const uint64_t immediate = esr & 0xffffu;

    if (exception_class == 0x15u && immediate == 0x0f02u) {
        /* Never let a scheduled process jump to the one-shot boot trampoline. */
        if (fahim_scheduler_is_active()) {
            console_write("EL0 process exit SVC: process ");
            write_hex64(fahim_scheduler_current_pid());
            console_write("\n");
            fahim_scheduler_exit_current();
        }

        /* Legacy one-shot EL0 smoke-test exit. */
        const uint64_t return_pc = fahim_address_space_user_return_pc();
        if (return_pc == 0u || fahim_address_space_deactivate_current() != 0) {
            console_write("EL0 SVC: failed to restore kernel TTBR0\n");
            exception_halt();
        }
        registers[31] = return_pc; /* ELR_EL1 in the saved trap frame */
        registers[32] = 0x3c5u;    /* EL1h, with DAIF masked */
        return;
    }

    /* SVC #0 is the initial AAPCS64 syscall ABI (x8 number, x0-x5 args). */
    if (exception_class == 0x15u && immediate == 0u &&
        fahim_scheduler_is_active()) {
        fahim_syscall_dispatch(registers);
        return;
    }

    if (fahim_scheduler_is_active()) {
        uint64_t elr;
        uint64_t far;
        __asm__ volatile("mrs %0, ELR_EL1" : "=r"(elr));
        __asm__ volatile("mrs %0, FAR_EL1" : "=r"(far));

        console_write("EL0 fault: process ");
        write_hex64(fahim_scheduler_current_pid());
        console_write(" EC=");
        write_hex64(exception_class);
        console_write(" ESR=");
        write_hex64(esr);
        console_write(" ELR=");
        write_hex64(elr);
        console_write(" FAR=");
        write_hex64(far);
        console_write("\n");

        /* A lower-EL fault kills only this process; other tasks keep running. */
        fahim_scheduler_fault_current();
    }

    exception_handle(registers, 8u);
}
