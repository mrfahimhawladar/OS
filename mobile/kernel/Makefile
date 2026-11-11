CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld

EXCEPTION_SELFTEST ?= 0
ifeq ($(EXCEPTION_SELFTEST),1)
BUILD := build/arm64-qemu-exception-test
SELFTEST_CFLAGS := -DFAHIM_EXCEPTION_SELFTEST=1
else
BUILD := build/arm64-qemu
SELFTEST_CFLAGS :=
endif

KERNEL := $(BUILD)/fahim-kernel.elf
MAP := $(BUILD)/fahim-kernel.map

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
          -ffreestanding -fno-builtin -fno-stack-protector \
          -fno-pic -fno-pie -mgeneral-regs-only \
          -mcpu=cortex-a57 -mstrict-align -Iinclude $(SELFTEST_CFLAGS)

ASFLAGS := $(CFLAGS)
LDFLAGS := -T linker/arm64-qemu.ld -nostdlib

SOURCES := core/main.c core/console/fdt_uart.c core/memory/fdt_memory.c \
           core/platform/fdt_gicv3.c core/exception.c
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES)) \
           $(BUILD)/arch/arm64/boot/entry.o

.PHONY: all clean inspect
all: $(KERNEL)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/arch/arm64/boot/entry.o: arch/arm64/boot/entry.S
	mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL): $(OBJECTS) linker/arm64-qemu.ld
	$(LD) $(LDFLAGS) -Map=$(MAP) -o $@ $(OBJECTS)

inspect: $(KERNEL)
	$(CROSS_COMPILE)readelf -h $(KERNEL)
	$(CROSS_COMPILE)size $(KERNEL)

clean:
	rm -rf build/arm64-qemu build/arm64-qemu-exception-test
