#include <stddef.h>
#include <stdint.h>

int console_init_from_fdt(const void *dtb);
void console_write(const char *text);

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile("msr daifset, #0xf\n\twfe" ::: "memory");
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
    console_write("\nMilestone 1: ARM64 boot + serial console.\n");

    halt_forever();
}
