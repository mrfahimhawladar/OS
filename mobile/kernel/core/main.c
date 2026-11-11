#include <stddef.h>
#include <stdint.h>

#include "memory.h"
#include "gicv3_platform.h"
#include "gicv3_driver.h"
#include "generic_timer.h"

extern uint8_t __kernel_start[];
extern uint8_t __kernel_end[];

int console_init_from_fdt(const void *dtb);
void console_write(const char *text);
void console_putc(char character);

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
    run_timer_irq_self_test();

#ifdef FAHIM_EXCEPTION_SELFTEST
    console_write("Triggering controlled BRK exception...\n");
    __asm__ volatile("brk #0xF05");
    console_write("Exception self-test: FAIL (BRK returned unexpectedly)\n");
#endif

    console_write("Milestone 4: exception vectors installed.\n");
    console_write("Milestone 5C: GICv3 IRQ dispatch and Generic Timer verified.\n");
    halt_forever();
}
