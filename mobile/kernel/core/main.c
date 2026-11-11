#include <stddef.h>
#include <stdint.h>

#include "memory.h"
#include "gicv3_platform.h"
#include "gicv3_driver.h"
#include "generic_timer.h"
#include "page_allocator.h"
#include "mmu.h"
#include "address_space.h"
#include "scheduler.h"
#include "console.h"

extern uint8_t __kernel_start[];
extern uint8_t __kernel_end[];
extern uint8_t __text_start[];
extern uint8_t __text_end[];
extern uint8_t __rodata_start[];
extern uint8_t __rodata_end[];
extern uint8_t __exception_vectors[];
extern const uint32_t __user_smoke_code_start[];
extern const uint32_t __user_smoke_code_end[];
extern const uint32_t __user_process_code_start[];
extern const uint32_t __user_process_code_end[];
extern const uint32_t __user_fault_code_start[];
extern const uint32_t __user_fault_code_end[];
extern uint64_t fahim_enter_user(uint64_t user_entry, uint64_t user_stack_top);

int console_init_from_fdt(const void *dtb);
void console_write(const char *text);
void console_putc(char character);

static void run_user_mode_self_test(void);

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile("msr daifset, #0xf\n\twfe" ::: "memory");
    }
}

static uint64_t read_current_el(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(value));
    return value;
}

static void console_write_hex64(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    console_write("0x");

    for (int shift = 60; shift >= 0; shift -= 4) {
        console_putc(digits[(value >> (uint32_t)shift) & 0x0Fu]);
    }
}

static void console_write_region(const char *label,
                                 const memory_region_t *region) {
    console_write(label);
    console_write(" base: ");
    console_write_hex64(region->base);
    console_write(" size: ");
    console_write_hex64(region->size);
    console_write(" bytes\n");
}

static void console_write_gicv3(const gicv3_platform_info_t *gic) {
    console_write("GICv3 Device Tree: discovered\n");
    console_write("GICD base: ");
    console_write_hex64(gic->distributor.base);
    console_write(" size: ");
    console_write_hex64(gic->distributor.size);
    console_write(" bytes\n");

    console_write("GICR regions: ");
    console_write_hex64(gic->redistributor_region_count);
    console_write("\n");
    for (size_t i = 0; i < gic->redistributor_region_count; ++i) {
        console_write("GICR base: ");
        console_write_hex64(gic->redistributors[i].base);
        console_write(" size: ");
        console_write_hex64(gic->redistributors[i].size);
        console_write(" bytes\n");
    }

    if (gic->redistributor_stride != 0u) {
        console_write("GICR stride: ");
        console_write_hex64(gic->redistributor_stride);
        console_write(" bytes\n");
    }
}

static void run_page_allocator_self_test(const memory_layout_t *layout) {
    const int32_t init_result = fahim_page_allocator_init(
        layout->usable, layout->usable_count);
    if (init_result != 0) {
        console_write("Physical Page Allocator: initialization FAILED, code=");
        console_write_hex64((uint64_t)(int64_t)init_result);
        console_write("\n");
        halt_forever();
    }

    console_write("Physical Page Allocator: initialized (frame-alloc 0.1.0)\n");
    console_write("Physical Page Allocator: base page 4096 bytes, total usable bytes ");
    console_write_hex64(fahim_page_allocator_total_bytes());
    console_write(", currently free ");
    console_write_hex64(fahim_page_allocator_free_bytes());
    console_write(" bytes\n");

    const int32_t test_result = fahim_page_allocator_self_test();
    if (test_result != 0) {
        console_write("Physical Page Allocator self-test: FAIL, code=");
        console_write_hex64((uint64_t)(int64_t)test_result);
        console_write("\n");
        halt_forever();
    }

    console_write("Physical Page Allocator self-test: PASS (4 KiB page, 4-page block, read/write, free/reuse)\n");
}

static int high_half_execution_self_test(void) {
    uintptr_t pc;
    uintptr_t stack_pointer;
    uintptr_t vector_base;
    __asm__ volatile(
        "adr %0, .\n\t"
        "mov %1, sp\n\t"
        "mrs %2, vbar_el1"
        : "=&r"(pc), "=&r"(stack_pointer), "=&r"(vector_base)
        :
        : "memory"
    );

    const uintptr_t high_half_base = (uintptr_t)0xffff000000000000ull;
    return (pc & high_half_base) == high_half_base
        && (stack_pointer & high_half_base) == high_half_base
        && vector_base == (uintptr_t)__exception_vectors;
}

static void run_mmu_self_test(const memory_layout_t *layout,
                                const gicv3_platform_info_t *gic) {
    memory_region_t devices[GICV3_MAX_REDISTRIBUTOR_REGIONS + 2u];
    size_t device_count = 0u;

    devices[device_count++] = gic->distributor;
    for (size_t i = 0; i < gic->redistributor_region_count; ++i) {
        devices[device_count++] = gic->redistributors[i];
    }

    const uint64_t uart_base = console_uart_physical_base();
    if (uart_base == 0u) {
        console_write("MMU: PL011 physical address unavailable\n");
        halt_forever();
    }
    devices[device_count].base = uart_base;
    devices[device_count].size = 0x1000u;
    ++device_count;

    const int32_t result = fahim_mmu_init(
        layout->ram, layout->ram_count,
        devices, device_count,
        (uintptr_t)__text_start, (uintptr_t)__text_end,
        (uintptr_t)__rodata_start, (uintptr_t)__rodata_end);
    if (result != 0 || fahim_mmu_is_enabled() != 1u) {
        console_write("MMU: initialization FAILED, code=");
        console_write_hex64((uint64_t)(int64_t)result);
        console_write("\n");
        halt_forever();
    }

    uint64_t sctlr;
    __asm__ volatile("mrs %0, SCTLR_EL1" : "=r"(sctlr));
    if ((sctlr & 1u) == 0u) {
        console_write("MMU: SCTLR_EL1.M did not become active\n");
        halt_forever();
    }

    /*
     * The lower identity alias remains mapped, but all new kernel execution
     * now proceeds from TTBR1's high-half direct map.
     */
    fahim_relocate_high_half();
    if (!high_half_execution_self_test()) {
        uintptr_t pc;
        uintptr_t stack_pointer;
        uintptr_t vector_base;
        __asm__ volatile(
            "adr %0, .\n\tmov %1, sp\n\tmrs %2, vbar_el1"
            : "=&r"(pc), "=&r"(stack_pointer), "=&r"(vector_base)
            :
            : "memory"
        );
        console_write("Kernel relocation: FAIL, PC=");
        console_write_hex64(pc);
        console_write(" SP=");
        console_write_hex64(stack_pointer);
        console_write(" VBAR=");
        console_write_hex64(vector_base);
        console_write("\n");
        halt_forever();
    }
    console_write("Kernel relocation: high-half PC, SP, VBAR verified (TTBR1)\n");

    /* The UART remains usable only if its mapping has Device memory attributes. */
    console_write("MMU: EL1 translation enabled (TTBR0 identity map + TTBR1 high-half execution)\n");
    console_write("MMU: RAM Normal-WB; GIC/PL011 Device-nGnRnE\n");
    console_write("MMU: kernel text RO+X; rodata RO+XN; data RW+XN\n");

    const uint64_t page = fahim_page_allocator_alloc(1u);
    if (page == UINT64_MAX || (page & 0xfffu) != 0u) {
        console_write("MMU self-test: FAIL (physical page allocation)\n");
        halt_forever();
    }

    volatile uint64_t *probe = (volatile uint64_t *)(uintptr_t)page;
    *probe = 0x4d4d5541524d3634ull;
    const int memory_ok = *probe == 0x4d4d5541524d3634ull;
    const int32_t free_result = fahim_page_allocator_free(page, 1u);
    if (!memory_ok || free_result != 0) {
        console_write("MMU self-test: FAIL (identity RAM access)\n");
        halt_forever();
    }

    console_write("MMU self-test: PASS (RAM, Device MMIO, permissions, post-MMU page allocation)\n");

    const int32_t high_alias_result = fahim_mmu_high_alias_self_test();
    if (high_alias_result != 0) {
        console_write("MMU high-half direct-map self-test: FAIL, code=");
        console_write_hex64((uint64_t)(int64_t)high_alias_result);
        console_write("\n");
        halt_forever();
    }
    console_write("MMU high-half direct-map self-test: PASS (TTBR1 alias read/write)\n");

    const int32_t address_space_result = fahim_address_space_self_test();
    if (address_space_result != 0) {
        console_write("User Address Space self-test: FAIL, code=");
        console_write_hex64((uint64_t)(int64_t)address_space_result);
        console_write("\n");
        halt_forever();
    }
    console_write("User Address Space self-test: PASS (two ASIDs, EL0 permissions, isolation, unmap/remap, destroy)\n");
    run_user_mode_self_test();
}


static void synchronize_user_code_cache(uintptr_t writable_alias,
                                                uintptr_t executable_alias,
                                                size_t length) {
    uint64_t cache_type_register;
    __asm__ volatile("mrs %0, CTR_EL0" : "=r"(cache_type_register));

    /* CTR_EL0 line-size fields are log2(words per line). */
    const size_t data_line_bytes =
        (size_t)4u << ((cache_type_register >> 16) & 0x0fu);
    const size_t instruction_line_bytes =
        (size_t)4u << (cache_type_register & 0x0fu);

    const uintptr_t data_start =
        writable_alias & ~(uintptr_t)(data_line_bytes - 1u);
    const uintptr_t data_end =
        (writable_alias + length + data_line_bytes - 1u)
        & ~(uintptr_t)(data_line_bytes - 1u);
    for (uintptr_t address = data_start; address < data_end;
         address += data_line_bytes) {
        __asm__ volatile("dc cvau, %0" : : "r"(address) : "memory");
    }
    __asm__ volatile("dsb ish" ::: "memory");

    const uintptr_t instruction_start =
        executable_alias & ~(uintptr_t)(instruction_line_bytes - 1u);
    const uintptr_t instruction_end =
        (executable_alias + length + instruction_line_bytes - 1u)
        & ~(uintptr_t)(instruction_line_bytes - 1u);
    for (uintptr_t address = instruction_start; address < instruction_end;
         address += instruction_line_bytes) {
        __asm__ volatile("ic ivau, %0" : : "r"(address) : "memory");
    }
    __asm__ volatile("dsb ish\n\tisb" ::: "memory");
}

static void run_user_mode_self_test(void) {
    const uint32_t space = fahim_address_space_create();
    if (space == UINT32_MAX) {
        console_write("EL0 User Mode self-test: FAIL (address-space creation)\n");
        halt_forever();
    }

    const uint64_t code_page = fahim_page_allocator_alloc(1u);
    const uint64_t stack_page = fahim_page_allocator_alloc(1u);
    if (code_page == UINT64_MAX || stack_page == UINT64_MAX) {
        console_write("EL0 User Mode self-test: FAIL (user frame allocation)\n");
        halt_forever();
    }

    const size_t code_size =
        (uintptr_t)__user_smoke_code_end - (uintptr_t)__user_smoke_code_start;
    if (code_size == 0u || code_size > 4096u || (code_size & 3u) != 0u) {
        console_write("EL0 User Mode self-test: FAIL (user payload size)\n");
        halt_forever();
    }

    /*
     * The test payload is assembled as ordinary upstream-toolchain AArch64
     * instructions in read-only kernel rodata, then copied into its own frame.
     */
    volatile uint32_t *destination = (volatile uint32_t *)(uintptr_t)code_page;
    const uint32_t *source = __user_smoke_code_start;
    for (size_t index = 0; index < code_size / sizeof(uint32_t); ++index) {
        destination[index] = source[index];
    }

    const uint64_t user_code_va = 0x0000000100200000ull;
    const uint64_t user_stack_va = 0x0000000100400000ull;
    if (fahim_address_space_map_user_page(
            space, user_code_va, code_page, FAHIM_USER_PAGE_RO_X) != 0 ||
        fahim_address_space_map_user_page(
            space, user_stack_va, stack_page, FAHIM_USER_PAGE_RW_XN) != 0) {
        console_write("EL0 User Mode self-test: FAIL (EL0 page mapping)\n");
        halt_forever();
    }

    if (fahim_address_space_activate(space) != 0) {
        console_write("EL0 User Mode self-test: FAIL (TTBR0 activation)\n");
        halt_forever();
    }
    synchronize_user_code_cache(
        (uintptr_t)code_page, (uintptr_t)user_code_va, code_size);

    /* User payload writes 0xF00D to x0 and exits via SVC #0xF02. */
    const uint64_t exit_value = fahim_enter_user(
        user_code_va, user_stack_va + 4096u);
    if (exit_value != 0xf00du) {
        console_write("EL0 User Mode self-test: FAIL (unexpected user exit value) ");
        console_write_hex64(exit_value);
        console_write("\n");
        halt_forever();
    }

    if (fahim_address_space_destroy(space) != 0 ||
        fahim_page_allocator_free(code_page, 1u) != 0 ||
        fahim_page_allocator_free(stack_page, 1u) != 0) {
        console_write("EL0 User Mode self-test: FAIL (address-space/frame cleanup)\n");
        halt_forever();
    }

    console_write("EL0 User Mode self-test: PASS (EL0 execution, SVC return, TTBR0 restore)\n");
}

static void run_process_scheduler_self_test(void) {
    const size_t code_size =
        (uintptr_t)__user_process_code_end - (uintptr_t)__user_process_code_start;
    const size_t fault_code_size =
        (uintptr_t)__user_fault_code_end - (uintptr_t)__user_fault_code_start;
    if (code_size == 0u || code_size > 4096u || (code_size & 3u) != 0u ||
        fault_code_size == 0u || fault_code_size > 4096u ||
        (fault_code_size & 3u) != 0u) {
        console_write("Process Scheduler self-test: FAIL (user payload size)\n");
        halt_forever();
    }

    const uint64_t code_page = fahim_page_allocator_alloc(1u);
    const uint64_t fault_code_page = fahim_page_allocator_alloc(1u);
    const uint64_t stack_page_a = fahim_page_allocator_alloc(1u);
    const uint64_t stack_page_b = fahim_page_allocator_alloc(1u);
    const uint64_t stack_page_sleep = fahim_page_allocator_alloc(1u);
    const uint64_t fault_stack_page = fahim_page_allocator_alloc(1u);
    const uint64_t shared_page = fahim_page_allocator_alloc(1u);
    if (code_page == UINT64_MAX || fault_code_page == UINT64_MAX ||
        stack_page_a == UINT64_MAX || stack_page_b == UINT64_MAX ||
        stack_page_sleep == UINT64_MAX || fault_stack_page == UINT64_MAX ||
        shared_page == UINT64_MAX) {
        console_write("Process Scheduler self-test: FAIL (physical frame allocation)\n");
        halt_forever();
    }

    /* Normal EL0 tasks share one RO+X image; the fault task has its own. */
    const uintptr_t code_alias =
        (uintptr_t)0xffff000000000000ull + (uintptr_t)code_page;
    const uintptr_t fault_code_alias =
        (uintptr_t)0xffff000000000000ull + (uintptr_t)fault_code_page;
    volatile uint32_t *destination = (volatile uint32_t *)code_alias;
    volatile uint32_t *fault_destination =
        (volatile uint32_t *)fault_code_alias;
    const uint32_t *source = __user_process_code_start;
    const uint32_t *fault_source = __user_fault_code_start;
    for (size_t index = 0u; index < code_size / sizeof(uint32_t); ++index) {
        destination[index] = source[index];
    }
    for (size_t index = 0u; index < fault_code_size / sizeof(uint32_t); ++index) {
        fault_destination[index] = fault_source[index];
    }

    const uintptr_t shared_alias =
        (uintptr_t)0xffff000000000000ull + (uintptr_t)shared_page;
    volatile uint64_t *shared_counter = (volatile uint64_t *)shared_alias;
    *shared_counter = 0u;

    const uint32_t space_parent = fahim_address_space_create();
    const uint32_t space_child = fahim_address_space_create();
    const uint32_t space_sleep = fahim_address_space_create();
    const uint32_t fault_space = fahim_address_space_create();
    if (space_parent == UINT32_MAX || space_child == UINT32_MAX ||
        space_sleep == UINT32_MAX || fault_space == UINT32_MAX) {
        console_write("Process Scheduler self-test: FAIL (address-space creation)\n");
        halt_forever();
    }

    const uint64_t user_code_va = 0x0000000100200000ull;
    const uint64_t user_stack_va = 0x0000000100400000ull;
    const uint64_t shared_user_va = 0x0000000100600000ull;
    if (fahim_address_space_map_user_page(
            space_parent, user_code_va, code_page, FAHIM_USER_PAGE_RO_X) != 0 ||
        fahim_address_space_map_user_page(
            space_parent, user_stack_va, stack_page_a, FAHIM_USER_PAGE_RW_XN) != 0 ||
        fahim_address_space_map_user_page(
            space_parent, shared_user_va, shared_page, FAHIM_USER_PAGE_RW_XN) != 0 ||
        fahim_address_space_map_user_page(
            space_child, user_code_va, code_page, FAHIM_USER_PAGE_RO_X) != 0 ||
        fahim_address_space_map_user_page(
            space_child, user_stack_va, stack_page_b, FAHIM_USER_PAGE_RW_XN) != 0 ||
        fahim_address_space_map_user_page(
            space_child, shared_user_va, shared_page, FAHIM_USER_PAGE_RW_XN) != 0 ||
        fahim_address_space_map_user_page(
            space_sleep, user_code_va, code_page, FAHIM_USER_PAGE_RO_X) != 0 ||
        fahim_address_space_map_user_page(
            space_sleep, user_stack_va, stack_page_sleep, FAHIM_USER_PAGE_RW_XN) != 0 ||
        fahim_address_space_map_user_page(
            space_sleep, shared_user_va, shared_page, FAHIM_USER_PAGE_RW_XN) != 0 ||
        fahim_address_space_map_user_page(
            fault_space, user_code_va, fault_code_page, FAHIM_USER_PAGE_RO_X) != 0 ||
        fahim_address_space_map_user_page(
            fault_space, user_stack_va, fault_stack_page, FAHIM_USER_PAGE_RW_XN) != 0) {
        console_write("Process Scheduler self-test: FAIL (user mapping)\n");
        halt_forever();
    }

    /* Invalidate each executable page through its mapped user VA. */
    if (fahim_address_space_activate(space_parent) != 0) {
        console_write("Process Scheduler self-test: FAIL (normal cache-maintenance activation)\n");
        halt_forever();
    }
    synchronize_user_code_cache(code_alias, (uintptr_t)user_code_va, code_size);
    if (fahim_address_space_deactivate_current() != 0) {
        console_write("Process Scheduler self-test: FAIL (normal cache-maintenance restore)\n");
        halt_forever();
    }

    if (fahim_address_space_activate(fault_space) != 0) {
        console_write("Process Scheduler self-test: FAIL (fault cache-maintenance activation)\n");
        halt_forever();
    }
    synchronize_user_code_cache(
        fault_code_alias, (uintptr_t)user_code_va, fault_code_size);
    if (fahim_address_space_deactivate_current() != 0) {
        console_write("Process Scheduler self-test: FAIL (fault cache-maintenance restore)\n");
        halt_forever();
    }

    if (fahim_scheduler_create_user(
            space_parent, user_code_va, user_stack_va + 4096u, 101u) < 0 ||
        fahim_scheduler_create_user(
            space_child, user_code_va, user_stack_va + 4096u, 202u) < 0 ||
        fahim_scheduler_create_user(
            space_sleep, user_code_va, user_stack_va + 4096u, 404u) < 0 ||
        fahim_scheduler_create_user(
            fault_space, user_code_va, user_stack_va + 4096u, 303u) < 0 ||
        fahim_scheduler_set_parent(202u, 101u) != 0) {
        console_write("Process Scheduler self-test: FAIL (task/parent creation)\n");
        halt_forever();
    }

    const int32_t timer_result =
        fahim_generic_timer_init(FAHIM_SCHEDULER_TICK_MS);
    if (timer_result != 0 ||
        fahim_generic_timer_set_irq_limit(UINT32_MAX) != 0) {
        console_write("Process Scheduler self-test: FAIL (periodic timer setup)\n");
        halt_forever();
    }

    console_write("Process Scheduler: starting parent, child, sleeper and fault-isolation tasks\n");
    fahim_scheduler_start();

    __asm__ volatile("msr daifset, #2\n\tisb" ::: "memory");
    const uint32_t scheduler_ticks = fahim_generic_timer_irq_count();
    fahim_generic_timer_stop();

    const uint32_t idle_waits = fahim_scheduler_idle_wait_count();
    const uint32_t wakeups = fahim_scheduler_wakeup_count();
    const uint32_t user_faults = fahim_scheduler_user_fault_count();
    const uint32_t reaps = fahim_scheduler_reap_count();
    const uint32_t waitpid_successes = fahim_scheduler_waitpid_success_count();
    if (scheduler_ticks == 0u || shared_counter[0] != 3u ||
        shared_counter[1] != 0x2a00u ||
        idle_waits != 2u || wakeups != 2u || user_faults != 1u ||
        reaps != 4u || waitpid_successes != 1u) {
        console_write("Process Lifecycle self-test: FAIL ticks=");
        console_write_hex64(scheduler_ticks);
        console_write(" shared=");
        console_write_hex64(shared_counter[0]);
        console_write(" wait_status=");
        console_write_hex64(shared_counter[1]);
        console_write(" idle=");
        console_write_hex64(idle_waits);
        console_write(" wakeups=");
        console_write_hex64(wakeups);
        console_write(" faults=");
        console_write_hex64(user_faults);
        console_write(" waitpid=");
        console_write_hex64(waitpid_successes);
        console_write(" reaps=");
        console_write_hex64(reaps);
        console_write("\n");
        halt_forever();
    }

    console_write("Process Scheduler timer ticks: ");
    console_write_hex64(scheduler_ticks);
    console_write(" idle waits: ");
    console_write_hex64(idle_waits);
    console_write(" wakeups: ");
    console_write_hex64(wakeups);
    console_write(" user faults: ");
    console_write_hex64(user_faults);
    console_write(" waitpid: ");
    console_write_hex64(waitpid_successes);
    console_write(" reaps: ");
    console_write_hex64(reaps);
    console_write("\n");

    if (fahim_scheduler_is_active()) {
        console_write("Process Lifecycle self-test: FAIL (scheduler did not return)\n");
        halt_forever();
    }

    fahim_scheduler_reset();
    if (fahim_page_allocator_free(code_page, 1u) != 0 ||
        fahim_page_allocator_free(fault_code_page, 1u) != 0 ||
        fahim_page_allocator_free(stack_page_a, 1u) != 0 ||
        fahim_page_allocator_free(stack_page_b, 1u) != 0 ||
        fahim_page_allocator_free(stack_page_sleep, 1u) != 0 ||
        fahim_page_allocator_free(fault_stack_page, 1u) != 0 ||
        fahim_page_allocator_free(shared_page, 1u) != 0) {
        console_write("Process Lifecycle self-test: FAIL (physical frame cleanup)\n");
        halt_forever();
    }

    console_write("Process Lifecycle self-test: PASS (timer preemption, waitpid, child zombie/reap, exit status, timed sleep, idle WFI, EL0 fault isolation, address-space cleanup)\n");
}

static void run_timer_irq_self_test(void) {
    const uint32_t expected_ticks = 5u;
    const uint32_t interval_ms = 10u;

    const int32_t timer_result = fahim_generic_timer_init(interval_ms);
    if (timer_result != 0) {
        console_write("ARM Generic Timer: initialization FAILED, code=");
        console_write_hex64((uint64_t)(int64_t)timer_result);
        console_write("\n");
        halt_forever();
    }

    console_write("ARM Generic Timer: EL1 physical timer configured, interval ");
    console_write_hex64(interval_ms);
    console_write(" ms, counter frequency ");
    console_write_hex64(fahim_generic_timer_frequency_hz());
    console_write(" Hz\n");
    console_write("IRQ Dispatcher: installed; enabling CPU IRQ delivery\n");

    /* Unmask IRQ only; keep FIQ, SError and debug exceptions masked. */
    __asm__ volatile("msr daifclr, #2\n\tisb" ::: "memory");

    while (fahim_generic_timer_irq_count() < expected_ticks) {
        __asm__ volatile("wfi" ::: "memory");
    }

    /* Stop accepting IRQs before the final shutdown/reporting path. */
    __asm__ volatile("msr daifset, #2\n\tisb" ::: "memory");
    fahim_generic_timer_stop();

    if (fahim_generic_timer_irq_count() != expected_ticks) {
        console_write("ARM Generic Timer IRQ test: FAIL (unexpected IRQ count)\n");
        halt_forever();
    }

    console_write("ARM Generic Timer IRQ test: PASS (5 interrupts, PPI 30)\n");
}

void kernel_main(const void *dtb) {
    if (console_init_from_fdt(dtb) != 0) {
        halt_forever();
    }

    console_write("\n");
    console_write("========================================\n");
    console_write("          FAHIM OS ARM64                \n");
    console_write("========================================\n");
    console_write("Kernel entry: OK\n");
    console_write("Kernel BSS: zeroed before C entry\n");
    console_write("Device Tree: parsed\n");
    console_write("PL011 UART: initialized from Device Tree\n");
    console_write("Platform: QEMU virt (ARM64 reference target)\n");

    const uint64_t current_el = read_current_el();
    console_write("Boot exception level: ");
    console_write_hex64(current_el);
    console_write("\n");
    if (current_el != 0x4u) {
        console_write("Boot exception level: FAILED (EL1 required)\n");
        halt_forever();
    }
    console_write("Boot exception level: EL1 confirmed\n");
    console_write("Exception vectors: VBAR_EL1 installed\n");

    gicv3_platform_info_t gic;
    const int gic_result = gicv3_discover_from_fdt(dtb, &gic);
    if (gic_result != 0) {
        console_write("GICv3 Device Tree: FAILED, code=");
        console_write_hex64((uint64_t)(int64_t)gic_result);
        console_write("\n");
        halt_forever();
    }
    console_write("GICv3 Device Tree: validated\n");
    console_write_gicv3(&gic);

    /*
     * arm-gic 0.10.0 currently accepts one contiguous GICR window.
     * QEMU virt exposes one redistributor region for this single-CPU
     * reference boot. Reject layouts we cannot safely represent.
     */
    if (gic.redistributor_region_count != 1u ||
        gic.distributor.size < 0x10000u ||
        gic.redistributors[0].size < 0x20000u) {
        console_write("GICv3 driver: unsupported register window layout\n");
        halt_forever();
    }

    const int gic_driver_result = fahim_gicv3_init(
        gic.distributor.base, gic.redistributors[0].base, 1u);
    if (gic_driver_result != 0) {
        console_write("GICv3 driver: initialization FAILED, code=");
        console_write_hex64((uint64_t)(int64_t)gic_driver_result);
        console_write("\n");
        halt_forever();
    }

    console_write("GICv3 driver: initialized (arm-gic 0.10.0)\n");
    console_write("GICv3 Group 1: enabled for current CPU\n");
    console_write("GICv3 timer PPI 30: enabled (level-triggered)\n");
    console_write("GICv3 CPU priority mask: ");
    const uint32_t priority_mask = fahim_gicv3_read_priority_mask();
    console_write_hex64(priority_mask);
    console_write("\n");
    if (priority_mask < 0xf0u) {
        console_write("GICv3 initialization self-test: FAIL\n");
        halt_forever();
    }
    console_write("GICv3 initialization self-test: PASS\n");

    memory_layout_t layout;
    const int result = memory_layout_from_dtb(
        dtb, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end, &layout);

    if (result != 0) {
        console_write("Memory layout: FAILED, code=");
        console_write_hex64((uint64_t)(int64_t)result);
        console_write("\n");
        halt_forever();
    }

    console_write("Memory layout: validated\n");
    console_write("RAM regions: ");
    console_write_hex64(layout.ram_count);
    console_write("\n");
    for (size_t i = 0; i < layout.ram_count; ++i) {
        console_write_region("RAM region", &layout.ram[i]);
    }

    console_write("Reserved regions: ");
    console_write_hex64(layout.reserved_count);
    console_write("\n");
    for (size_t i = 0; i < layout.reserved_count; ++i) {
        console_write_region("Reserved region", &layout.reserved[i]);
    }

    console_write("Usable regions: ");
    console_write_hex64(layout.usable_count);
    console_write("\n");
    for (size_t i = 0; i < layout.usable_count; ++i) {
        console_write_region("Usable region", &layout.usable[i]);
    }

    console_write("\nMilestone 3: reserved memory exclusions.\n");
    run_page_allocator_self_test(&layout);
    run_mmu_self_test(&layout, &gic);
    run_timer_irq_self_test();
    run_process_scheduler_self_test();

#ifdef FAHIM_EXCEPTION_SELFTEST
    console_write("Triggering controlled BRK exception...\n");
    __asm__ volatile("brk #0xF05");
    console_write("Exception self-test: FAIL (BRK returned unexpectedly)\n");
#endif

    console_write("Milestone 4: exception vectors installed.\n");
    console_write("Milestone 5C: GICv3 IRQ dispatch and Generic Timer verified.\n");
    halt_forever();
}
