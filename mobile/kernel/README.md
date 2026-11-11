# Fahim OS Kernel

This is the device-independent ARM64 kernel implementation. The root-level x86 kernel.cpp is a separate learning prototype and is not reused as the mobile kernel.

## Verified milestone 1: ARM64 boot + serial console

- AArch64 assembly entry point and bootstrap stack
- Freestanding C kernel entry
- Flattened Device Tree (FDT) header and structure parsing
- PL011 UART discovery through Device Tree compatible and reg properties
- Polled serial text output

## Verified milestone 2: FDT RAM discovery

- Reads enabled Device Tree nodes with device_type = "memory"
- Uses parent bus address-cells and size-cells to decode each reg tuple
- Supports one-cell and two-cell addresses and sizes
- Handles multiple RAM tuples and rejects malformed or overflowing ranges

## Milestone 3: reserved memory exclusions

The memory-layout builder combines FDT memreserve entries, enabled static children of reserved-memory, the complete kernel image, and the DTB blob. It merges reservations and subtracts them from discovered RAM to produce usable ranges. Dynamic reserved-memory allocations are rejected until a placement allocator exists.

## Milestone 4: ARM64 exception vectors and fatal diagnostics

The boot path clears BSS, normalizes EL2 entry to EL1h, installs a 2 KiB-aligned 16-slot VBAR_EL1 vector table, and reports fatal exception state over PL011. CI triggers a controlled BRK #0xF05 and checks the expected vector and syndrome.

## Milestone 5A: GICv3 platform discovery

The QEMU reference machine is explicitly configured with gic-version=3. The kernel discovers the enabled arm,gic-v3 node from the boot Device Tree, validates its register tuples, and reports the Distributor and Redistributor MMIO regions.

This is platform discovery only. It does not initialize the GIC, enable IRQs, or implement interrupt dispatch. These responsibilities will be supplied by a ready-made upstream GICv3 driver adapted to Fahim OS's small kernel interface rather than reimplemented here.

The parser accepts one or more Redistributor regions up to the early-boot limit and validates optional #redistributor-regions and redistributor-stride properties.

## Reference platform and verification

GitHub Actions cross-compiles and boots the kernel in QEMU virt with 512 MiB RAM and GICv3 explicitly selected. It checks the RAM/reserved/usable ranges, GICv3 DT resources, and the controlled synchronous-exception test.

[Open the ARM64 QEMU workflow](https://github.com/mrfahimhawladar/OS/actions/workflows/arm64-kernel.yml).

## Build locally

    make -C mobile/kernel CROSS_COMPILE=aarch64-linux-gnu-

Build the separate exception-test image with:

    make -C mobile/kernel CROSS_COMPILE=aarch64-linux-gnu- EXCEPTION_SELFTEST=1

Production image: mobile/kernel/build/arm64-qemu/fahim-kernel.elf

Exception-test image: mobile/kernel/build/arm64-qemu-exception-test/fahim-kernel.elf

## Important limits

- Usable memory is detected and reported only; no physical allocator uses it yet.
- Dynamic reserved-memory allocations and page-aligned allocation are not supported.
- Exception reporting is fatal-only; IRQ/FIQ/SError delivery is not exercised yet.
- GICv3 resources are discovered, but the GIC driver is not initialized and interrupts remain masked.
- The generic timer, scheduler, heap, storage, input, graphics, and touchscreen are not implemented.
- This is not an Android image, phone ROM, or flashable artifact. QEMU success does not prove support for a real smartphone.

## Next milestones

1. Adapt and integrate a ready-made GICv3 driver
2. Integrate the ARM Generic Timer and test IRQ delivery
3. Add a physical page allocator using validated, page-aligned usable RAM
4. Add MMU/page tables
5. Add scheduler and context switching
