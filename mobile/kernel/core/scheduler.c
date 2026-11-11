#include <stddef.h>
#include <stdint.h>

#include "address_space.h"
#include "scheduler.h"

/*
 * Scheduling and Context layout follow the small AArch64 task-switching
 * implementation in KNCn23/mini-rtos (kernel/task.[ch], kernel/sched.[ch]).
 * Fahim OS keeps its own task/address-space ownership and uses the existing
 * upstream aarch64-paging bridge to switch each process's TTBR0.
 */

typedef struct {
    uint64_t x19, x20, x21, x22;
    uint64_t x23, x24, x25, x26;
    uint64_t x27, x28;
    uint64_t fp;
    uint64_t lr;
    uint64_t sp;
} fahim_task_context_t;

_Static_assert(sizeof(fahim_task_context_t) == 104u,
               "AArch64 context layout must match context_switch.S");

typedef enum {
    FAHIM_TASK_READY,
    FAHIM_TASK_RUNNING,
    FAHIM_TASK_DEAD
} fahim_task_state_t;

typedef struct {
    fahim_task_context_t context;
    fahim_task_state_t state;
    uint32_t pid;
    uint32_t address_space;
    uint64_t user_entry;
    uint64_t user_stack_top;
} fahim_task_t;

#define KERNEL_STACK_BYTES 16384u

static fahim_task_t tasks[FAHIM_MAX_USER_TASKS];
static uint8_t kernel_stacks[FAHIM_MAX_USER_TASKS][KERNEL_STACK_BYTES]
    __attribute__((aligned(16)));

static fahim_task_context_t bootstrap_context;
static int current_task = -1;
static uint32_t task_count;
static volatile int scheduler_active;

extern void fahim_context_switch(fahim_task_context_t *previous,
                                 const fahim_task_context_t *next);
extern void fahim_user_task_trampoline(void);

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
    for (size_t i = 0; i < sizeof(*task); ++i) {
        ((uint8_t *)task)[i] = 0u;
    }

    task->pid = (uint32_t)pid;
    task->address_space = address_space;
    task->user_entry = entry;
    task->user_stack_top = user_stack_top;
    task->state = FAHIM_TASK_READY;

    uintptr_t stack_top = (uintptr_t)&kernel_stacks[index][KERNEL_STACK_BYTES];
    stack_top &= ~(uintptr_t)0x0fu;

    task->context.x19 = entry;                  /* task_trampoline: ELR_EL1 */
    task->context.x20 = user_stack_top;         /* task_trampoline: SP_EL0 */
    task->context.x21 = pid;                    /* initial EL0 x0 */
    task->context.lr = (uintptr_t)fahim_user_task_trampoline;
    task->context.sp = stack_top;
    return (int)index;
}

static int pick_next_task(void) {
    if (task_count == 0u) {
        return -1;
    }

    const int start = current_task;
    for (uint32_t step = 1u; step <= task_count; ++step) {
        const uint32_t index = (uint32_t)((start + (int)step + (int)task_count)
                                          % (int)task_count);
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

static void switch_to_task(int next_index) {
    const int previous_index = current_task;
    const uint32_t next_space = tasks[next_index].address_space;
    const uint32_t previous_space = previous_index >= 0
        ? tasks[previous_index].address_space : UINT32_MAX;

    if (fahim_address_space_switch(next_space) != 0) {
        for (;;) {
            __asm__ volatile("wfe" ::: "memory");
        }
    }

    if (previous_index >= 0 &&
        tasks[previous_index].state == FAHIM_TASK_RUNNING) {
        tasks[previous_index].state = FAHIM_TASK_READY;
    }
    tasks[next_index].state = FAHIM_TASK_RUNNING;
    current_task = next_index;

    (void)previous_space;
    if (previous_index >= 0) {
        fahim_context_switch(&tasks[previous_index].context,
                             &tasks[next_index].context);
    } else {
        fahim_context_switch(&bootstrap_context, &tasks[next_index].context);
    }
}

void fahim_scheduler_start(void) {
    if (scheduler_active || task_count == 0u) {
        return;
    }

    scheduler_active = 1;
    const int first = pick_next_task();
    if (first >= 0) {
        switch_to_task(first);
    }

    /* The final task exits by switching back to this saved boot context. */
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

void fahim_scheduler_tick(void) {
    /* Round-robin preemption uses the same switch path as cooperative yield. */
    fahim_scheduler_yield();
}

void fahim_scheduler_exit_current(void) {
    if (!scheduler_active || current_task < 0) {
        for (;;) {
            __asm__ volatile("wfe" ::: "memory");
        }
    }

    const int previous = current_task;
    tasks[previous].state = FAHIM_TASK_DEAD;
    const int next = pick_next_task();

    if (next >= 0) {
        switch_to_task(next);
    }

    /* No runnable tasks remain: restore the kernel's original TTBR0 and stack. */
    if (fahim_address_space_switch(UINT32_MAX) != 0) {
        for (;;) {
            __asm__ volatile("wfe" ::: "memory");
        }
    }
    current_task = -1;
    fahim_context_switch(&tasks[previous].context, &bootstrap_context);

    for (;;) {
        __asm__ volatile("wfe" ::: "memory");
    }
}

uint64_t fahim_scheduler_current_pid(void) {
    return current_task >= 0 ? tasks[current_task].pid : UINT64_MAX;
}

int fahim_scheduler_is_active(void) {
    return scheduler_active;
}

void fahim_scheduler_reset(void) {
    if (scheduler_active) {
        return;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        tasks[i].state = FAHIM_TASK_DEAD;
        tasks[i].address_space = UINT32_MAX;
    }
    task_count = 0u;
    current_task = -1;
}
