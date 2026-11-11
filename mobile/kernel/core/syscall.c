#include <stddef.h>
#include <stdint.h>

#include "console.h"
#include "scheduler.h"
#include "syscall.h"

typedef uint64_t (*syscall_handler_t)(uint64_t arg0, uint64_t arg1);

static void write_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    console_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        console_putc(digits[(value >> (uint32_t)shift) & 0x0fu]);
    }
}

static uint64_t syscall_exit(uint64_t status, uint64_t unused) {
    (void)status;
    (void)unused;
    fahim_scheduler_exit_current(status);
}

static uint64_t syscall_yield(uint64_t unused0, uint64_t unused1) {
    (void)unused0;
    (void)unused1;
    fahim_scheduler_yield();
    return 0u;
}

static uint64_t syscall_getpid(uint64_t unused0, uint64_t unused1) {
    (void)unused0;
    (void)unused1;
    return fahim_scheduler_current_pid();
}

static uint64_t syscall_sleep(uint64_t milliseconds, uint64_t unused) {
    (void)unused;
    const int result = fahim_scheduler_sleep_current(milliseconds);
    return (uint64_t)(int64_t)result;
}

static uint64_t syscall_log(uint64_t pid, uint64_t marker) {
    console_write("EL0 process ");
    write_hex(pid);
    console_write(" ran marker ");
    write_hex(marker);
    console_write("\n");
    return 0u;
}

/* Small dispatch table: each entry is an upstream-style AAPCS64 handler. */
static const syscall_handler_t syscall_table[] = {
    [FAHIM_SYS_EXIT] = syscall_exit,
    [FAHIM_SYS_YIELD] = syscall_yield,
    [FAHIM_SYS_GETPID] = syscall_getpid,
    [FAHIM_SYS_LOG] = syscall_log,
    [FAHIM_SYS_SLEEP] = syscall_sleep,
};

void fahim_syscall_dispatch(uint64_t *saved_registers) {
    if (saved_registers == NULL) {
        return;
    }

    const uint64_t number = saved_registers[8]; /* x8 */
    if (number == FAHIM_SYS_WAITPID) {
        int64_t exit_status = 0;
        const int64_t child_pid =
            fahim_scheduler_waitpid((uint32_t)saved_registers[0], &exit_status);
        saved_registers[0] = (uint64_t)child_pid;
        saved_registers[1] = (uint64_t)exit_status;
        return;
    }
    if (number >= (sizeof(syscall_table) / sizeof(syscall_table[0])) ||
        syscall_table[number] == NULL) {
        saved_registers[0] = (uint64_t)(int64_t)-38; /* -ENOSYS */
        return;
    }

    /* AAPCS64 syscall arguments are x0 and x1 for these initial services. */
    saved_registers[0] = syscall_table[number](
        saved_registers[0], saved_registers[1]);
}
