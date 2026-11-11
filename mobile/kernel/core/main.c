#include <stddef.h>
#include <stdint.h>

#include "memory.h"
#include "gicv3_platform.h"

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
    console_write("GICv3 driver: not initialized yet\n");
    console_write("Interrupts remain masked\n");
    console_write_gicv3(&gic);

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

#ifdef FAHIM_EXCEPTION_SELFTEST
    console_write("Triggering controlled BRK exception...\n");
    __asm__ volatile("brk #0xF05");
    console_write("Exception self-test: FAIL (BRK returned unexpectedly)\n");
#endif

    console_write("Milestone 4: exception vectors installed.\n");
    console_write("Milestone 5A: GICv3 platform resources discovered.\n");
    halt_forever();
}
