# Fahim OS Kernel

This is the device-independent ARM64 kernel implementation. The root-level x86 kernel.cpp is a separate learning prototype and is not reused as the mobile kernel.

## Verified milestone 1: ARM64 boot + serial console

- AArch64 assembly entry point and bootstrap stack
- Freestanding C kernel entry
- Flattened Device Tree (FDT) header and structure parsing
- PL011 serial UART discovery via Device Tree compatible and reg properties
- Polled serial text output

## Verified milestone 2: FDT RAM discovery

- Reads enabled Device Tree nodes with device_type = "memory"
- Uses the parent bus's address-cells and size-cells when decoding each reg tuple
- Supports one-cell and two-cell addresses and sizes
- Handles multiple RAM tuples and rejects malformed or overflowing ranges
- Reports each discovered RAM region over the serial console

## Milestone 3: reserved memory exclusions

The memory-layout builder now combines the following reservations:

- FDT memreserve entries
- Enabled children of reserved-memory that provide static reg ranges
- The complete kernel image, including its bootstrap stack and BSS
- The DTB blob passed to the kernel

It sorts and merges overlapping/adjacent reservations, then subtracts them from the reported RAM ranges to produce separate usable RAM ranges. Disabled reserved-memory nodes are ignored. Dynamic reserved-memory nodes that specify size but have no reg are rejected safely because the kernel does not yet have a placement allocator.

The kernel linker exports kernel_start and kernel_end so that the full loaded image can be excluded from usable RAM.

## Milestone 4: ARM64 exception vectors and fatal diagnostics

The entry path now:

- Clears the complete linker-defined BSS range before entering C. This includes zero-initialized globals and the bootstrap stack, so the stack is assigned only after the clear operation.
- Accepts EL1 or EL2 firmware entry. EL2 entry is normalized to AArch64 EL1h with DAIF masked; EL3 entry is not yet supported.
- Selects SP_EL1, installs a 2 KiB-aligned VBAR_EL1 table containing all 16 architectural vector slots, and executes barriers before entering C.
- Saves x0-x30 in a 256-byte, 16-byte-aligned frame before calling the C exception reporter.
- Captures ESR_EL1, ELR_EL1, FAR_EL1, SPSR_EL1, CurrentEL, vector index, and selected saved GPRs over the existing polling PL011 console.
- Masks further exceptions during reporting and halts on an unhandled exception instead of attempting an unsafe return with ERET.

GitHub Actions builds both the production kernel and a separate test image. The test image executes BRK #0xF05; CI checks that the EL1 SPx synchronous vector is selected, the syndrome matches the expected BRK class/immediate, and the diagnostic path halts.

## Reference platform and verification

The initial reference platform is QEMU generic ARM64 virt with 512 MiB RAM. GitHub Actions cross-compiles the kernel, boots it in QEMU, checks RAM/reserved/usable ranges, and runs a controlled synchronous-exception test.

[Open the latest ARM64 QEMU workflow](https://github.com/mrfahimhawladar/OS/actions/workflows/arm64-kernel.yml).

## Build locally

Install an AArch64 Linux cross-toolchain, then:

    make -C mobile/kernel CROSS_COMPILE=aarch64-linux-gnu-

Build the separate controlled exception-test image with:

    make -C mobile/kernel CROSS_COMPILE=aarch64-linux-gnu- EXCEPTION_SELFTEST=1

Production image: mobile/kernel/build/arm64-qemu/fahim-kernel.elf

Exception-test image: mobile/kernel/build/arm64-qemu-exception-test/fahim-kernel.elf

## Important limits

- The usable memory map is currently detected and reported only. The kernel does not allocate memory from it yet.
- Dynamic reserved-memory allocations are not supported.
- Page alignment and platform-specific reserved regions will need explicit treatment before a page allocator can use this map.
- Exception logging has a polling UART and fatal-halt path; it is not yet an IRQ dispatcher, scheduler signal path, or recoverable fault subsystem.
- IRQ/FIQ/SError vector slots are installed but are not yet exercised by a GIC or asynchronous fault source.
- Only EL1/EL2 firmware entry is supported. EL3 entry is not yet normalized.
- The kernel still lacks a physical page allocator, MMU/page tables, interrupt controller driver, generic timer, scheduler, heap, storage, input, graphics, and touchscreen support.
- This is not an Android image, phone ROM, or flashable artifact.
- QEMU success does not imply support for any real smartphone.

## Next milestones

1. Add the GIC and generic timer
2. Implement a physical page allocator with page-aligned usable ranges
3. Add MMU/page tables
4. Add scheduler and context switching
