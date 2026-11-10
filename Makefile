CXX := g++
AS := as
LD := ld

CXXFLAGS := -m32 -std=gnu++17 -ffreestanding -O2 -Wall -Wextra \
            -fno-exceptions -fno-rtti -fno-threadsafe-statics \
            -fno-use-cxa-atexit -fno-stack-protector -fno-pic \
            -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -mno-mmx -mno-sse -mno-sse2
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/iso
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := dist/fahim-os.iso

.PHONY: all clean

all: $(ISO)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/kernel.o: kernel.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/boot.o: boot.s | $(BUILD_DIR)
	$(AS) --32 $< -o $@

$(KERNEL): $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o
	grub-file --is-x86-multiboot $@

$(ISO): $(KERNEL) grub.cfg
	mkdir -p $(ISO_DIR)/boot/grub dist
	cp $(KERNEL) $(ISO_DIR)/boot/kernel.elf
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_DIR)

clean:
	rm -rf $(BUILD_DIR) dist
