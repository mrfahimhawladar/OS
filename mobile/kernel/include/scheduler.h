#ifndef FAHIM_OS_SCHEDULER_H
#define FAHIM_OS_SCHEDULER_H

#include <stdint.h>

#define FAHIM_MAX_USER_TASKS 4u

int fahim_scheduler_create_user(uint32_t address_space,
                                uint64_t entry,
                                uint64_t user_stack_top,
                                uint64_t pid);
void fahim_scheduler_start(void);
void fahim_scheduler_yield(void);
void fahim_scheduler_exit_current(void) __attribute__((noreturn));
void fahim_scheduler_tick(void);
uint64_t fahim_scheduler_current_pid(void);
int fahim_scheduler_is_active(void);
void fahim_scheduler_reset(void);

#endif
