# Fahim OS Kernel

This is the new device-independent ARM64 kernel implementation. The root-level x86 `kernel.cpp` is a separate learning prototype and is not reused as the mobile kernel.

## Verified milestone 1: ARM64 boot + serial console

- AArch64 assembly entry point and bootstrap stack
- Freestanding C kernel entry
- Flattened Device Tree (FDT) header and structure parsing
- PL011 serial UART discovery via Device Tree `compatible` and `reg` properties
- Polled serial text output

## Milestone 2: FDT RAM discovery

- Reads enabled Device Tree nodes with `device_type = "memory"`
- Uses the parent bus's `#address-cells` and `#size-cells` when decoding each `reg` tuple
- Supports one-cell and two-cell addresses and sizes
- Handles multiple RAM tuples and rejects malformed or overflowing ranges
- Reports each discovered RAM region over the serial console
- The QEMU CI test checks the expected 512 MiB RAM region at `0x40000000`

The first reference target is QEMU's generic ARM64 `virt` platform. QEMU places its bare-metal DTB at the start of RAM (0x40000000) and reserves a 1 MiB region for it. The kernel ELF is linked at 0x40200000 to keep the image clear of that reservation.

**Verification:** GitHub Actions compiles the AArch64 ELF, boots it in QEMU, and checks the serial banner, UART initialization, detected RAM base, and RAM size. See the [ARM64 kernel workflow](https://github.com/mrfahimhawladar/OS/actions/workflows/arm64-kernel.yml).

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

- The memory map is currently detection/reporting only; no memory is allocated from it.
- Reserved-memory nodes and boot-time exclusions are not yet combined into a safe allocatable map. A physical page allocator must not use the reported RAM ranges until those reservations, the kernel image, and the DTB are excluded.
- MMU/page tables, interrupt controller, timer, scheduler, heap, storage, input, graphics, and touchscreen are not implemented yet.
- This is not an Android image, phone ROM, or flashable artifact.
- QEMU success does not imply support for any real smartphone.

## Next milestones

1. Parse reserved-memory and establish an explicit reserved-region map
2. Add exception vectors and safe exception reporting
3. Add the GIC and generic timer
4. Implement a physical page allocator that only uses verified free RAM
5. Add MMU/page tables, then scheduler and context switching
