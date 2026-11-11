CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)gcc

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
LDFLAGS := -nostdlib -static -Wl,-T,linker/arm64-qemu.ld \
           -Wl,-Map=$(MAP) -Wl,--build-id=none

RUST_TARGET := aarch64-unknown-none
RUST_TARGET_DIR := build/rust-target
RUST_LIB := $(RUST_TARGET_DIR)/$(RUST_TARGET)/release/libfahim_gic_bridge.a
RUSTFLAGS ?=
KERNEL_RUSTFLAGS := $(RUSTFLAGS) -C target-feature=-neon,-fp-armv8

SOURCES := core/main.c core/console/fdt_uart.c core/memory/fdt_memory.c \
           core/platform/fdt_gicv3.c core/exception.c \
           core/scheduler.c
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES)) \
           $(BUILD)/arch/arm64/boot/entry.o \
           $(BUILD)/arch/arm64/boot/context_switch.o

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

$(RUST_LIB): Makefile rust/Cargo.toml rust/rust-toolchain.toml rust/src/lib.rs
	cd rust && CARGO_TARGET_DIR=../$(RUST_TARGET_DIR) RUSTFLAGS="$(KERNEL_RUSTFLAGS)" cargo build --target $(RUST_TARGET) --release

$(KERNEL): $(OBJECTS) $(RUST_LIB) linker/arm64-qemu.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS) $(RUST_LIB) \
		-Wl,--start-group -lgcc -Wl,--end-group

inspect: $(KERNEL)
	$(CROSS_COMPILE)readelf -h $(KERNEL)
	$(CROSS_COMPILE)size $(KERNEL)

clean:
	rm -rf build/arm64-qemu build/arm64-qemu-exception-test
