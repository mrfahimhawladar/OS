#ifndef FAHIM_OS_SCHEDULER_H
#define FAHIM_OS_SCHEDULER_H

#include <stdint.h>

#define FAHIM_MAX_USER_TASKS 4u
#define FAHIM_SCHEDULER_TICK_MS 10u

int fahim_scheduler_create_user(uint32_t address_space,
                                uint64_t entry,
                                uint64_t user_stack_top,
                                uint64_t pid);
void fahim_scheduler_start(void);
void fahim_scheduler_yield(void);
int fahim_scheduler_sleep_current(uint64_t milliseconds);
void fahim_scheduler_exit_current(void) __attribute__((noreturn));
void fahim_scheduler_tick(void);
uint64_t fahim_scheduler_current_pid(void);
int fahim_scheduler_is_active(void);
uint32_t fahim_scheduler_idle_wait_count(void);
uint32_t fahim_scheduler_wakeup_count(void);
uint32_t fahim_scheduler_user_fault_count(void);
void fahim_scheduler_fault_current(void) __attribute__((noreturn));
void fahim_scheduler_reset(void);

#endif
