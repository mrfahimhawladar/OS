# Fahim OS Kernel

This is the new ARM64 kernel implementation. The root-level x86 `kernel.cpp` is a separate learning prototype and is not reused as the mobile kernel.

## Implemented milestone 1

- AArch64 assembly entry point and bootstrap stack
- Freestanding C kernel entry
- Flattened Device Tree (FDT) header and structure parsing
- PL011 serial UART discovery via Device Tree `compatible` and `reg` properties
- Polled serial text output

The first target is QEMU's generic ARM64 `virt` platform. QEMU places its bare-metal DTB at the start of RAM (0x40000000) and reserves a 1 MiB region for it. The kernel ELF is linked at 0x40200000 to keep the image clear of that reservation. The UART is discovered from the DTB rather than assuming a phone-specific hardware address.

## Build

Install an AArch64 Linux cross-toolchain, then:

```sh
make -C mobile/kernel CROSS_COMPILE=aarch64-linux-gnu-
```

Output:

```
mobile/kernel/build/arm64-qemu/fahim-kernel.elf
```

The `.github/workflows/arm64-kernel.yml` workflow builds the ELF and runs a QEMU serial-console smoke test.

## Important limits

- This is only an early serial-console kernel milestone.
- MMU/page tables, interrupt controller, timer, scheduler, heap, storage, input, graphics, and touchscreen are not implemented yet.
- It is not an Android image, phone ROM, or flashable artifact.
- QEMU success does not imply support for any real smartphone.

## Next milestones

1. Validate FDT memory nodes and create a memory map
2. Add exception vectors and safe exception reporting
3. Add the GIC and generic timer
4. Add physical page allocator and MMU
5. Add scheduler and context switching
