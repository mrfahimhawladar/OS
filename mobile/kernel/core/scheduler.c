#include <stddef.h>
#include <stdint.h>

#include "address_space.h"
#include "scheduler.h"

/* AArch64 context-switch foundation adapted from KNCn23/mini-rtos. */
typedef struct {
    uint64_t x19, x20, x21, x22;
    uint64_t x23, x24, x25, x26;
    uint64_t x27, x28;
    uint64_t fp, lr, sp;
} fahim_task_context_t;

_Static_assert(sizeof(fahim_task_context_t) == 104u,
               "AArch64 context layout must match context_switch.S");

typedef enum {
    FAHIM_TASK_READY,
    FAHIM_TASK_RUNNING,
    FAHIM_TASK_SLEEPING,
    FAHIM_TASK_WAITING_CHILD,
    FAHIM_TASK_ZOMBIE,
    FAHIM_TASK_DEAD
} fahim_task_state_t;

typedef struct {
    fahim_task_context_t context;
    fahim_task_state_t state;
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t role;
    uint32_t address_space;
    uint64_t user_entry;
    uint64_t user_stack_top;
    uint64_t wake_at_tick;
    uint32_t wait_target_pid;
    int64_t exit_status;
} fahim_task_t;

#define KERNEL_STACK_BYTES 16384u

static fahim_task_t tasks[FAHIM_MAX_USER_TASKS];
static uint8_t kernel_stacks[FAHIM_MAX_USER_TASKS][KERNEL_STACK_BYTES]
    __attribute__((aligned(16)));

static fahim_task_context_t bootstrap_context;
static int current_task = -1;
static uint32_t task_count;
static volatile int scheduler_active;
/* Fail closed at boot: only trusted lock-screen/emergency tasks may run. */
static volatile int session_locked = 1;
static uint64_t scheduler_ticks;
static uint32_t scheduler_idle_waits;
static uint32_t scheduler_wakeups;
static uint32_t scheduler_user_faults;
static uint32_t scheduler_reaps;
static uint32_t scheduler_waitpid_successes;
static uint32_t scheduler_locked_task_skips;

extern void fahim_context_switch(fahim_task_context_t *previous,
                                 const fahim_task_context_t *next);
extern void fahim_user_task_trampoline(void);

static void scheduler_halt(void) __attribute__((noreturn));
static void scheduler_halt(void) {
    for (;;) {
        __asm__ volatile("wfe" ::: "memory");
    }
}

static int find_task_by_pid(uint32_t pid) {
    for (uint32_t i = 0u; i < task_count; ++i) {
        if (tasks[i].state != FAHIM_TASK_DEAD && tasks[i].pid == pid) {
            return (int)i;
        }
    }
    return -1;
}

static int has_live_tasks(void) {
    for (uint32_t i = 0u; i < task_count; ++i) {
        if (tasks[i].state != FAHIM_TASK_DEAD) {
            return 1;
        }
    }
    return 0;
}

static int task_role_allowed(uint32_t role) {
    if (role > (uint32_t)FAHIM_TASK_ROLE_EMERGENCY) {
        return 0;
    }
    return session_locked == 0 ||
        role == (uint32_t)FAHIM_TASK_ROLE_LOCKSCREEN ||
        role == (uint32_t)FAHIM_TASK_ROLE_EMERGENCY;
}

int fahim_scheduler_role_allowed(fahim_task_role_t role) {
    return task_role_allowed((uint32_t)role);
}

int fahim_scheduler_session_locked(void) {
    return session_locked != 0;
}

int fahim_scheduler_set_session_locked(uint32_t locked) {
    if (locked > 1u) {
        return -1;
    }
    /* EL0 may change the session only from the kernel-assigned Lock Screen role. */
    if (scheduler_active &&
        (current_task < 0 ||
         tasks[current_task].role != (uint32_t)FAHIM_TASK_ROLE_LOCKSCREEN)) {
        return -2;
    }
    session_locked = (int)locked;
    return 0;
}

static int pick_next_task(void) {
    if (task_count == 0u) {
        return -1;
    }

    const int start = current_task;
    for (uint32_t step = 1u; step <= task_count; ++step) {
        const uint32_t index =
            (uint32_t)((start + (int)step + (int)task_count) % (int)task_count);
        if (tasks[index].state == FAHIM_TASK_READY) {
            if (!task_role_allowed(tasks[index].role)) {
                scheduler_locked_task_skips++;
                continue;
            }
            return (int)index;
        }
    }

    if (current_task >= 0 && tasks[current_task].state == FAHIM_TASK_READY) {
        if (!task_role_allowed(tasks[current_task].role)) {
            scheduler_locked_task_skips++;
        } else {
            return current_task;
        }
    }
    return -1;
}

static void wake_expired_tasks(void) {
    for (uint32_t i = 0u; i < task_count; ++i) {
        fahim_task_t *task = &tasks[i];
        if (task->state == FAHIM_TASK_SLEEPING &&
            task->wake_at_tick <= scheduler_ticks) {
            task->wake_at_tick = 0u;
            task->state = FAHIM_TASK_READY;
            scheduler_wakeups++;
        }
    }
}

static void switch_to_task(int next_index) {
    const int previous_index = current_task;
    const uint32_t next_space = tasks[next_index].address_space;

    if (fahim_address_space_switch(next_space) != 0) {
        scheduler_halt();
    }

    if (previous_index >= 0 &&
        tasks[previous_index].state == FAHIM_TASK_RUNNING) {
        tasks[previous_index].state = FAHIM_TASK_READY;
    }
    tasks[next_index].state = FAHIM_TASK_RUNNING;
    current_task = next_index;

    if (previous_index >= 0) {
        fahim_context_switch(&tasks[previous_index].context,
                             &tasks[next_index].context);
    } else {
        fahim_context_switch(&bootstrap_context, &tasks[next_index].context);
    }
}

static void switch_to_idle(void) {
    const int previous_index = current_task;
    if (previous_index < 0) {
        return;
    }

    if (tasks[previous_index].state == FAHIM_TASK_RUNNING) {
        tasks[previous_index].state = FAHIM_TASK_READY;
    }

    if (fahim_address_space_switch(UINT32_MAX) != 0) {
        scheduler_halt();
    }

    current_task = -1;
    if (has_live_tasks()) {
        scheduler_idle_waits++;
    }

    fahim_context_switch(&tasks[previous_index].context, &bootstrap_context);
}

/* Drop an inactive process translation and its ASID/page-table hierarchy. */
static void reap_task_address_space(uint32_t index) {
    const uint32_t handle = tasks[index].address_space;
    if (handle == UINT32_MAX) {
        return;
    }
    if (fahim_address_space_destroy(handle) != 0) {
        scheduler_halt();
    }
    tasks[index].address_space = UINT32_MAX;
    scheduler_reaps++;
}

static void reap_orphaned_zombies(uint32_t parent_pid) {
    for (uint32_t i = 0u; i < task_count; ++i) {
        if (tasks[i].parent_pid != parent_pid) {
            continue;
        }
        tasks[i].parent_pid = 0u;
        if (tasks[i].state == FAHIM_TASK_ZOMBIE) {
            reap_task_address_space(i);
            tasks[i].state = FAHIM_TASK_DEAD;
        }
    }
}

static void wake_waiting_parent(uint32_t child_index) {
    const uint32_t parent_pid = tasks[child_index].parent_pid;
    if (parent_pid == 0u) {
        return;
    }
    const int parent_index = find_task_by_pid(parent_pid);
    if (parent_index < 0) {
        tasks[child_index].parent_pid = 0u;
        return;
    }

    fahim_task_t *parent = &tasks[parent_index];
    if (parent->state == FAHIM_TASK_WAITING_CHILD &&
        (parent->wait_target_pid == 0u ||
         parent->wait_target_pid == tasks[child_index].pid)) {
        parent->wait_target_pid = 0u;
        parent->state = FAHIM_TASK_READY;
    }
}

int fahim_scheduler_create_user(uint32_t address_space,
                                uint64_t entry,
                                uint64_t user_stack_top,
                                uint64_t pid) {
    return fahim_scheduler_create_user_with_role(
        address_space, entry, user_stack_top, pid,
        FAHIM_TASK_ROLE_APPLICATION);
}

int fahim_scheduler_create_user_with_role(uint32_t address_space,
                                          uint64_t entry,
                                          uint64_t user_stack_top,
                                          uint64_t pid,
                                          fahim_task_role_t role) {
    if (scheduler_active || task_count >= FAHIM_MAX_USER_TASKS ||
        address_space == UINT32_MAX || entry == 0u || user_stack_top == 0u ||
        pid > UINT32_MAX || find_task_by_pid((uint32_t)pid) >= 0 ||
        (uint32_t)role > (uint32_t)FAHIM_TASK_ROLE_EMERGENCY) {
        return -1;
    }

    const uint32_t index = task_count++;
    fahim_task_t *task = &tasks[index];
    for (size_t i = 0u; i < sizeof(*task); ++i) {
        ((uint8_t *)task)[i] = 0u;
    }

    task->pid = (uint32_t)pid;
    task->address_space = address_space;
    task->user_entry = entry;
    task->user_stack_top = user_stack_top;
    task->role = (uint32_t)role;
    task->state = FAHIM_TASK_READY;

    uintptr_t stack_top =
        (uintptr_t)&kernel_stacks[index][KERNEL_STACK_BYTES];
    stack_top &= ~(uintptr_t)0x0fu;

    task->context.x19 = entry;
    task->context.x20 = user_stack_top;
    task->context.x21 = pid;
    task->context.lr = (uintptr_t)fahim_user_task_trampoline;
    task->context.sp = stack_top;
    return (int)index;
}

int fahim_scheduler_set_parent(uint32_t child_pid, uint32_t parent_pid) {
    if (scheduler_active || child_pid == parent_pid) {
        return -1;
    }
    const int child = find_task_by_pid(child_pid);
    if (child < 0) {
        return -2;
    }
    if (parent_pid == 0u) {
        tasks[child].parent_pid = 0u;
        return 0;
    }
    const int parent = find_task_by_pid(parent_pid);
    if (parent < 0) {
        return -3;
    }

    /* Reject a cycle before linking the parent chain. */
    uint32_t ancestor = parent_pid;
    for (uint32_t depth = 0u; depth < task_count; ++depth) {
        if (ancestor == child_pid) {
            return -4;
        }
        const int index = find_task_by_pid(ancestor);
        if (index < 0 || tasks[index].parent_pid == 0u) {
            break;
        }
        ancestor = tasks[index].parent_pid;
    }

    tasks[child].parent_pid = parent_pid;
    return 0;
}

void fahim_scheduler_start(void) {
    if (scheduler_active || task_count == 0u) {
        return;
    }

    scheduler_ticks = 0u;
    scheduler_idle_waits = 0u;
    scheduler_wakeups = 0u;
    scheduler_user_faults = 0u;
    scheduler_reaps = 0u;
    scheduler_waitpid_successes = 0u;
    scheduler_locked_task_skips = 0u;
    scheduler_active = 1;

    const int first = pick_next_task();
    if (first >= 0) {
        switch_to_task(first);
    }

    while (has_live_tasks()) {
        __asm__ volatile(
            "msr daifclr, #2\n\t"
            "isb\n\t"
            "wfi"
            ::: "memory"
        );
    }

    __asm__ volatile("msr daifset, #2\n\tisb" ::: "memory");
    current_task = -1;
    scheduler_active = 0;
}

void fahim_scheduler_yield(void) {
    if (!scheduler_active || current_task < 0) {
        return;
    }

    tasks[current_task].state = FAHIM_TASK_READY;
    const int next = pick_next_task();
    if (next >= 0 && next != current_task) {
        switch_to_task(next);
    } else if (current_task >= 0) {
        tasks[current_task].state = FAHIM_TASK_RUNNING;
    }
}

int fahim_scheduler_sleep_current(uint64_t milliseconds) {
    if (!scheduler_active || current_task < 0) {
        return -1;
    }

    if (milliseconds == 0u) {
        fahim_scheduler_yield();
        return 0;
    }

    const uint64_t tick_ms = FAHIM_SCHEDULER_TICK_MS;
    uint64_t delay_ticks = milliseconds / tick_ms;
    if ((milliseconds % tick_ms) != 0u) {
        delay_ticks++;
    }
    if (delay_ticks == 0u) {
        delay_ticks = 1u;
    }

    fahim_task_t *task = &tasks[current_task];
    task->wake_at_tick = delay_ticks > (UINT64_MAX - scheduler_ticks)
        ? UINT64_MAX : scheduler_ticks + delay_ticks;
    task->state = FAHIM_TASK_SLEEPING;

    const int next = pick_next_task();
    if (next >= 0) {
        switch_to_task(next);
    } else {
        switch_to_idle();
    }

    return 0;
}

int64_t fahim_scheduler_waitpid(uint32_t target_pid, int64_t *exit_status) {
    if (!scheduler_active || current_task < 0 || exit_status == NULL) {
        return -1;
    }

    const uint32_t parent_pid = tasks[current_task].pid;
    for (;;) {
        int matching_child = -1;
        int zombie_child = -1;
        for (uint32_t i = 0u; i < task_count; ++i) {
            if (tasks[i].state == FAHIM_TASK_DEAD ||
                tasks[i].parent_pid != parent_pid ||
                (target_pid != 0u && tasks[i].pid != target_pid)) {
                continue;
            }
            matching_child = (int)i;
            if (tasks[i].state == FAHIM_TASK_ZOMBIE) {
                zombie_child = (int)i;
                break;
            }
        }

        if (matching_child < 0) {
            return -10; /* ECHILD: no matching child exists */
        }

        if (zombie_child >= 0) {
            fahim_task_t *child = &tasks[zombie_child];
            const uint32_t child_pid = child->pid;
            *exit_status = child->exit_status;
            reap_task_address_space((uint32_t)zombie_child);
            child->state = FAHIM_TASK_DEAD;
            child->parent_pid = 0u;
            scheduler_waitpid_successes++;
            return (int64_t)child_pid;
        }

        tasks[current_task].wait_target_pid = target_pid;
        tasks[current_task].state = FAHIM_TASK_WAITING_CHILD;

        const int next = pick_next_task();
        if (next >= 0) {
            switch_to_task(next);
        } else {
            switch_to_idle();
        }
        /* Child exit wakes this task; re-check its zombie record on resume. */
    }
}

void fahim_scheduler_tick(void) {
    if (!scheduler_active) {
        return;
    }

    scheduler_ticks++;
    wake_expired_tasks();

    if (current_task >= 0 &&
        tasks[current_task].state == FAHIM_TASK_RUNNING) {
        tasks[current_task].state = FAHIM_TASK_READY;
    }

    const int next = pick_next_task();
    if (next >= 0 && next != current_task) {
        switch_to_task(next);
    } else if (next >= 0) {
        tasks[next].state = FAHIM_TASK_RUNNING;
    }
}

static void scheduler_exit_current_with_status(int64_t status)
    __attribute__((noreturn));

static void scheduler_exit_current_with_status(int64_t status) {
    if (!scheduler_active || current_task < 0) {
        scheduler_halt();
    }

    const int previous = current_task;
    const uint32_t exiting_pid = tasks[previous].pid;
    tasks[previous].exit_status = status;
    tasks[previous].wake_at_tick = 0u;

    const int parent = tasks[previous].parent_pid != 0u
        ? find_task_by_pid(tasks[previous].parent_pid) : -1;
    if (parent >= 0 && tasks[parent].state != FAHIM_TASK_DEAD) {
        tasks[previous].state = FAHIM_TASK_ZOMBIE;
        wake_waiting_parent((uint32_t)previous);
    } else {
        tasks[previous].parent_pid = 0u;
        tasks[previous].state = FAHIM_TASK_DEAD;
        reap_orphaned_zombies(exiting_pid);

        /* Restore the kernel TTBR0 before destroying this inactive page table. */
        if (fahim_address_space_switch(UINT32_MAX) != 0) {
            scheduler_halt();
        }
        reap_task_address_space((uint32_t)previous);
    }

    const int next = pick_next_task();
    if (next >= 0) {
        switch_to_task(next);
    } else {
        switch_to_idle();
    }

    scheduler_halt();
}

void fahim_scheduler_exit_current(uint64_t status) {
    scheduler_exit_current_with_status((int64_t)status);
}

uint64_t fahim_scheduler_current_pid(void) {
    return current_task >= 0 ? tasks[current_task].pid : UINT64_MAX;
}

int fahim_scheduler_is_active(void) {
    return scheduler_active;
}

uint32_t fahim_scheduler_idle_wait_count(void) {
    return scheduler_idle_waits;
}

uint32_t fahim_scheduler_wakeup_count(void) {
    return scheduler_wakeups;
}

uint32_t fahim_scheduler_user_fault_count(void) {
    return scheduler_user_faults;
}

uint32_t fahim_scheduler_reap_count(void) {
    return scheduler_reaps;
}

uint32_t fahim_scheduler_waitpid_success_count(void) {
    return scheduler_waitpid_successes;
}

uint32_t fahim_scheduler_locked_task_skip_count(void) {
    return scheduler_locked_task_skips;
}

void fahim_scheduler_fault_current(void) {
    if (!scheduler_active || current_task < 0) {
        scheduler_halt();
    }
    scheduler_user_faults++;
    scheduler_exit_current_with_status(-11);
}

void fahim_scheduler_reset(void) {
    if (scheduler_active) {
        return;
    }
    for (uint32_t i = 0u; i < task_count; ++i) {
        tasks[i].state = FAHIM_TASK_DEAD;
        tasks[i].address_space = UINT32_MAX;
        tasks[i].parent_pid = 0u;
        tasks[i].wake_at_tick = 0u;
        tasks[i].wait_target_pid = 0u;
    }
    task_count = 0u;
    current_task = -1;
    scheduler_ticks = 0u;
    scheduler_idle_waits = 0u;
    scheduler_wakeups = 0u;
    scheduler_user_faults = 0u;
    scheduler_reaps = 0u;
    scheduler_waitpid_successes = 0u;
    scheduler_locked_task_skips = 0u;
}
