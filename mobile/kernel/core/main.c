#include <stddef.h>
#include <stdint.h>

#include "memory.h"

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

void kernel_main(const void *dtb) {
    if (console_init_from_fdt(dtb) != 0) {
        halt_forever();
    }

    console_write("\n");
    console_write("========================================\n");
    console_write("          FAHIM OS ARM64                \n");
    console_write("========================================\n");
    console_write("Kernel entry: OK\n");
    console_write("Device Tree: parsed\n");
    console_write("PL011 UART: initialized from Device Tree\n");
    console_write("Platform: QEMU virt (ARM64 reference target)\n");

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
    halt_forever();
}
