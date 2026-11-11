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
    FAHIM_TASK_DEAD
} fahim_task_state_t;

typedef struct {
    fahim_task_context_t context;
    fahim_task_state_t state;
    uint32_t pid;
    uint32_t address_space;
    uint64_t user_entry;
    uint64_t user_stack_top;
    uint64_t wake_at_tick;
} fahim_task_t;

#define KERNEL_STACK_BYTES 16384u

static fahim_task_t tasks[FAHIM_MAX_USER_TASKS];
static uint8_t kernel_stacks[FAHIM_MAX_USER_TASKS][KERNEL_STACK_BYTES]
    __attribute__((aligned(16)));

static fahim_task_context_t bootstrap_context;
static int current_task = -1;
static uint32_t task_count;
static volatile int scheduler_active;
static uint64_t scheduler_ticks;
static uint32_t scheduler_idle_waits;
static uint32_t scheduler_wakeups;
static uint32_t scheduler_user_faults;

extern void fahim_context_switch(fahim_task_context_t *previous,
                                 const fahim_task_context_t *next);
extern void fahim_user_task_trampoline(void);

static void scheduler_halt(void) __attribute__((noreturn));
static void scheduler_halt(void) {
    for (;;) {
        __asm__ volatile("wfe" ::: "memory");
    }
}

static int has_live_tasks(void) {
    for (uint32_t i = 0u; i < task_count; ++i) {
        if (tasks[i].state != FAHIM_TASK_DEAD) {
            return 1;
        }
    }
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
            return (int)index;
        }
    }

    if (current_task >= 0 &&
        tasks[current_task].state == FAHIM_TASK_READY) {
        return current_task;
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
        /* bootstrap_context is the kernel idle/boot continuation. */
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

int fahim_scheduler_create_user(uint32_t address_space,
                                uint64_t entry,
                                uint64_t user_stack_top,
                                uint64_t pid) {
    if (scheduler_active || task_count >= FAHIM_MAX_USER_TASKS ||
        address_space == UINT32_MAX || entry == 0u || user_stack_top == 0u ||
        pid > UINT32_MAX) {
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

void fahim_scheduler_start(void) {
    if (scheduler_active || task_count == 0u) {
        return;
    }

    scheduler_ticks = 0u;
    scheduler_idle_waits = 0u;
    scheduler_wakeups = 0u;
    scheduler_user_faults = 0u;
    scheduler_active = 1;

    const int first = pick_next_task();
    if (first >= 0) {
        switch_to_task(first);
    }

    /* If all remaining tasks sleep, WFI until the Generic Timer wakes one. */
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

void fahim_scheduler_exit_current(void) {
    if (!scheduler_active || current_task < 0) {
        scheduler_halt();
    }

    const int previous = current_task;
    tasks[previous].state = FAHIM_TASK_DEAD;
    tasks[previous].wake_at_tick = 0u;

    const int next = pick_next_task();
    if (next >= 0) {
        switch_to_task(next);
    } else {
        switch_to_idle();
    }

    scheduler_halt();
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

void fahim_scheduler_fault_current(void) {
    if (!scheduler_active || current_task < 0) {
        scheduler_halt();
    }
    scheduler_user_faults++;
    fahim_scheduler_exit_current();
}


void fahim_scheduler_reset(void) {
    if (scheduler_active) {
        return;
    }
    for (uint32_t i = 0u; i < task_count; ++i) {
        tasks[i].state = FAHIM_TASK_DEAD;
        tasks[i].address_space = UINT32_MAX;
        tasks[i].wake_at_tick = 0u;
    }
    task_count = 0u;
    current_task = -1;
    scheduler_ticks = 0u;
    scheduler_idle_waits = 0u;
    scheduler_wakeups = 0u;
    scheduler_user_faults = 0u;
}
