# Fahim OS Kernel

This is the device-independent ARM64 kernel implementation. The root-level x86 `kernel.cpp` is a separate learning prototype and is not reused as the mobile kernel.

## Verified milestone 1: ARM64 boot + serial console

- AArch64 assembly entry point and bootstrap stack
- Freestanding C kernel entry
- Flattened Device Tree (FDT) header and structure parsing
- PL011 serial UART discovery via Device Tree `compatible` and `reg` properties
- Polled serial text output

## Verified milestone 2: FDT RAM discovery

- Reads enabled Device Tree nodes with `device_type = "memory"`
- Uses the parent bus's `#address-cells` and `#size-cells` when decoding each `reg` tuple
- Supports one-cell and two-cell addresses and sizes
- Handles multiple RAM tuples and rejects malformed or overflowing ranges
- Reports each discovered RAM region over the serial console

## Milestone 3: reserved memory exclusions

The memory-layout builder now combines the following reservations:

- FDT `/memreserve/` entries
- Enabled children of `/reserved-memory` that provide static `reg` ranges
- The complete kernel image, including its bootstrap stack and BSS
- The DTB blob passed to the kernel

It sorts and merges overlapping/adjacent reservations, then subtracts them from the reported RAM ranges to produce separate **usable RAM ranges**. Disabled reserved-memory nodes are ignored. Dynamic reserved-memory nodes that specify `size` but have no `reg` are rejected safely because the kernel does not yet have a placement allocator.

The kernel linker exports `__kernel_start` and `__kernel_end` so that the full loaded image can be excluded from usable RAM.

## Reference platform and verification

The initial reference platform is QEMU's generic ARM64 `virt` machine with 512 MiB RAM. GitHub Actions cross-compiles the kernel, boots it in QEMU, and checks the RAM, reserved and usable range output.

[Open the latest ARM64 QEMU workflow](https://github.com/mrfahimhawladar/OS/actions/workflows/arm64-kernel.yml).

## Build locally

Install an AArch64 Linux cross-toolchain, then:

```sh
make -C mobile/kernel CROSS_COMPILE=aarch64-linux-gnu-
```

Output:

```
mobile/kernel/build/arm64-qemu/fahim-kernel.elf
```

## Important limits

- The usable map is currently detected and reported only. The kernel does not allocate memory from it yet.
- Dynamic `/reserved-memory` allocations are not supported.
- Page alignment and platform-specific reserved regions will need explicit treatment before a page allocator can use this map.
- MMU/page tables, interrupt controller, timer, scheduler, heap, storage, input, graphics, and touchscreen are not implemented yet.
- This is not an Android image, phone ROM, or flashable artifact.
- QEMU success does not imply support for any real smartphone.

## Next milestones

1. Add exception vectors and safe exception reporting
2. Add the GIC and generic timer
3. Implement a physical page allocator with page-aligned usable ranges
4. Add MMU/page tables
5. Add scheduler and context switching
