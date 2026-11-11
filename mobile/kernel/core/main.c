#include <stddef.h>
#include <stdint.h>

#include "memory.h"

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

    memory_region_t regions[MEMORY_MAP_MAX_REGIONS];
    size_t region_count = 0;
    const int memory_result = fdt_memory_map_from_dtb(
        dtb, regions, MEMORY_MAP_MAX_REGIONS, &region_count);

    if (memory_result != 0) {
        console_write("FDT memory map: FAILED\n");
        halt_forever();
    }

    console_write("FDT memory map: detected\n");
    for (size_t i = 0; i < region_count; ++i) {
        console_write("RAM region base: ");
        console_write_hex64(regions[i].base);
        console_write("\nRAM region size: ");
        console_write_hex64(regions[i].size);
        console_write(" bytes\n");
    }

    console_write("\nMilestone 2: FDT RAM discovery.\n");
    halt_forever();
}
