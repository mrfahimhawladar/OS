CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld

BUILD := build/arm64-qemu
KERNEL := $(BUILD)/fahim-kernel.elf
MAP := $(BUILD)/fahim-kernel.map

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
          -ffreestanding -fno-builtin -fno-stack-protector \
          -fno-pic -fno-pie -mgeneral-regs-only \
          -mcpu=cortex-a57 -mstrict-align -Iinclude

ASFLAGS := $(CFLAGS)
LDFLAGS := -T linker/arm64-qemu.ld -nostdlib

SOURCES := core/main.c core/console/fdt_uart.c core/memory/fdt_memory.c
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES)) $(BUILD)/arch/arm64/boot/entry.o

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
	rm -rf $(BUILD)
