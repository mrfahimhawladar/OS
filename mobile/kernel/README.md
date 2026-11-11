# Fahim OS Kernel

This directory is reserved for the new ARM64 kernel. The existing root-level x86 `kernel.cpp` is a separate experiment and must not be reused as the mobile kernel.

## Design baseline

- Architecture: AArch64 / ARMv8-A
- Kernel style: modular monolithic, initially
- Core language: C
- CPU-specific code: AArch64 Assembly
- Freestanding build: cross-compiler, linker script, explicit memory map
- First boot environment: QEMU `virt`
- Device configuration: Device Tree, not phone-model checks scattered through core code

## Initial source layout (planned)

```text
mobile/kernel/
  arch/arm64/
    boot/
    exceptions/
    context/
    mmu/
  core/
    console/
    panic/
    scheduler/
    sync/
    ipc/
  mm/
    physical/
    virtual/
    heap/
  drivers/
    irq/
    timer/
    serial/
    virtio/
  fs/
  include/
  linker/
```

## Implementation order

1. AArch64 boot entry + linker layout
2. PL011 UART console + panic output
3. Device Tree (FDT) reader
4. Physical page allocator
5. Exception vectors + GIC + generic timer
6. MMU mapping and kernel heap
7. Scheduler and context switching
8. VFS and RAM filesystem

No source implementation or bootable ARM64 kernel exists yet. Each stage must be compiled and tested in QEMU before the next is claimed complete.
