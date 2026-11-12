#ifndef FAHIM_OS_SCHEDULER_H
#define FAHIM_OS_SCHEDULER_H

#include <stdint.h>

#define FAHIM_MAX_USER_TASKS 5u
#define FAHIM_SCHEDULER_TICK_MS 10u

typedef enum {
    FAHIM_TASK_ROLE_APPLICATION = 0,
    FAHIM_TASK_ROLE_LOCKSCREEN = 1,
    FAHIM_TASK_ROLE_EMERGENCY = 2,
    /* Trusted system shell; runnable only after the kernel session unlocks. */
    FAHIM_TASK_ROLE_SYSTEM_SHELL = 3
} fahim_task_role_t;

int fahim_scheduler_create_user(uint32_t address_space,
                                uint64_t entry,
                                uint64_t user_stack_top,
                                uint64_t pid);
int fahim_scheduler_create_user_with_role(uint32_t address_space,
                                          uint64_t entry,
                                          uint64_t user_stack_top,
                                          uint64_t pid,
                                          fahim_task_role_t role);
int fahim_scheduler_set_session_locked(uint32_t locked);
int fahim_scheduler_session_locked(void);
int fahim_scheduler_role_allowed(fahim_task_role_t role);
void fahim_scheduler_start(void);
int fahim_scheduler_set_parent(uint32_t child_pid, uint32_t parent_pid);
int64_t fahim_scheduler_waitpid(uint32_t target_pid, int64_t *exit_status);
void fahim_scheduler_yield(void);
int fahim_scheduler_sleep_current(uint64_t milliseconds);
void fahim_scheduler_exit_current(uint64_t status) __attribute__((noreturn));
void fahim_scheduler_tick(void);
uint64_t fahim_scheduler_current_pid(void);
int fahim_scheduler_is_active(void);
uint32_t fahim_scheduler_idle_wait_count(void);
uint32_t fahim_scheduler_wakeup_count(void);
uint32_t fahim_scheduler_user_fault_count(void);
uint32_t fahim_scheduler_reap_count(void);
uint32_t fahim_scheduler_waitpid_success_count(void);
uint32_t fahim_scheduler_locked_task_skip_count(void);
void fahim_scheduler_fault_current(void) __attribute__((noreturn));
void fahim_scheduler_reset(void);

#endif
