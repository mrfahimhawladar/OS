#ifndef FAHIM_OS_SYSCALL_H
#define FAHIM_OS_SYSCALL_H

#include <stdint.h>

/* Minimal AArch64 EL0 ABI: x8=number, x0-x5=arguments, x0=return value. */
#define FAHIM_SYS_EXIT  1u
#define FAHIM_SYS_YIELD 2u
#define FAHIM_SYS_GETPID 3u
#define FAHIM_SYS_LOG   4u
#define FAHIM_SYS_SLEEP 5u
#define FAHIM_SYS_WAITPID 6u

void fahim_syscall_dispatch(uint64_t *saved_registers);

#endif
