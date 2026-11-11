#ifndef FAHIM_OS_EXCEPTION_H
#define FAHIM_OS_EXCEPTION_H

#include <stdint.h>

/* The saved frame contains x0-x30 in 31 consecutive 64-bit slots. */
void exception_handle(const uint64_t *registers, uint64_t vector)
    __attribute__((noreturn));

#endif
