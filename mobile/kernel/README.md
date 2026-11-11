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

## Verified milestone 4: ARM64 exception vectors and fatal diagnostics

The boot path clears BSS, normalizes EL2 entry to EL1h, installs a 2 KiB-aligned 16-slot VBAR_EL1 vector table, and reports fatal exception state over PL011. CI triggers a controlled BRK #0xF05 and checks the expected vector and syndrome. The current-EL SPx IRQ vector now saves the interrupted general-purpose register frame, calls the IRQ dispatcher, restores the frame, and returns with ERET.

## Milestone 5A: GICv3 platform discovery

The QEMU reference machine is explicitly configured with gic-version=3. The kernel discovers the enabled arm,gic-v3 node from the boot Device Tree, validates its register tuples, and reports the Distributor and Redistributor MMIO regions.

The parser accepts one or more Redistributor regions up to the early-boot limit and validates optional #redistributor-regions and redistributor-stride properties.

## Verified milestone 5B: upstream GICv3 driver integration

Fahim OS integrates the maintained arm-gic 0.10.0 driver from ArmFirmwareCrates / TrustedFirmware.org through a small no_std Rust static-library bridge. The driver remains upstream code; Fahim OS supplies only the C ABI adapter and build integration.

The QEMU single-core path validates the current register-window layout, constructs the upstream GICv3 driver, initializes the Redistributor and Distributor, enables Group 1 for the boot CPU, and configures the CPU priority mask. The EL2 entry path sets ICC_SRE_EL2.SRE and ICC_SRE_EL2.Enable so EL1 can access the GIC CPU-interface system registers.

The bridge currently targets one contiguous Redistributor region and one boot CPU, matching the current QEMU reference setup. Other topologies are rejected rather than silently misconfigured.

## Verified milestone 5C: IRQ dispatcher + ARM Generic Timer

The interrupt path uses the existing upstream arm-gic CPU-interface API. GicCpuInterface::handle_interrupt(InterruptGroup::Group1, ...) performs interrupt acknowledge, runs the source handler, and automatically writes EOI after the handler returns. The exception stub preserves x0-x30, restores the interrupted frame, and uses ERET to resume the interrupted kernel context.

The EL1 Physical Timer is implemented by the upstream TrustedFirmware.org arm-generic-timer 0.3.1 crate (sysreg::PhysicalTimer and its Timer abstraction). The kernel configures timer PPI INTID 30 as level-triggered through arm-gic, then runs a five-interrupt QEMU test at a 10 ms interval. Each timer event reprograms the timer before the upstream GIC handler issues EOI; after the fifth event, the timer interrupt is masked. CPU IRQ delivery is unmasked only after the GIC, timer and exception dispatcher are ready.

This test proves the single-core QEMU reference path for timer programming, IRQ exception entry/return, GIC acknowledge, source dispatch, source deassertion and EOI. It does not yet prove SMP routing or physical-phone interrupt behavior.

Third-party source and licensing details are recorded in THIRD_PARTY_NOTICES.md. The upstream GIC and Generic Timer crates are dual-licensed under MIT OR Apache-2.0.

## Reference platform and verification

GitHub Actions cross-compiles and boots the kernel in QEMU virt with 512 MiB RAM and GICv3 explicitly selected. It checks the RAM/reserved/usable ranges, GICv3 initialization, five timer IRQ deliveries, and the controlled synchronous-exception test.

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
- The current exception diagnostics are fatal for synchronous exceptions, FIQ and SError. Only the current-EL SPx IRQ vector has a returning dispatcher path so far.
- The upstream GICv3 driver is currently initialized for the single-core QEMU layout. The active dispatcher handles the EL1 physical timer PPI; general device IRQ registration, SPI routing, SMP and interrupt nesting remain future work.
- The generic timer is now functional on the QEMU reference target. The scheduler, heap, storage, input, graphics, and touchscreen are not implemented.
- This is not an Android image, phone ROM, or flashable artifact. QEMU success does not prove support for a real smartphone.

## Next milestones

1. Add a physical page allocator using validated, page-aligned usable RAM
2. Add MMU/page tables and establish device-memory mappings
3. Add scheduler and context switching
4. Add the storage, input, display and touchscreen driver frameworks
5. Develop a physical-device platform port after the memory and interrupt foundations are stable
