# QEMU ARM64 reference platform

**Role:** first virtual development environment for Fahim OS.

This target is a generic ARM64 virtual machine, not a POCO/Xiaomi phone. It is intended to test the portable kernel and generic drivers before any physical device port.

## Virtual hardware

The QEMU `virt` machine provides standard virtual components such as:
- ARMv8/ARM64 CPU execution
- RAM and a generated Device Tree Blob (DTB)
- Generic interrupt controller (GIC)
- ARM architected timer
- PL011 serial console
- VirtIO devices for later block, network, display, and input experiments

QEMU's bare-metal boot mode places the generated DTB at the start of RAM (0x40000000). Other device locations must be discovered from the DTB rather than hard-coded in generic code. See the [QEMU virt documentation](https://www.qemu.org/docs/master/system/arm/virt).

## Implemented first kernel milestone

- ARM64 assembly entry and stack setup
- Freestanding C kernel entry
- FDT structure parsing to discover the PL011 UART
- Serial console boot banner

Source lives under `mobile/kernel/`. The GitHub Actions workflow `Build Fahim OS ARM64 Kernel` builds the ELF image and runs a serial-console smoke test in QEMU.

## First boot goal

The kernel should print:

```text
FAHIM OS ARM64
Kernel entry: OK
Device Tree: parsed
PL011 UART: initialized from Device Tree
Platform: QEMU virt (ARM64 reference target)
```

## What this platform does not prove

A successful QEMU boot does not prove support for any real smartphone. Commercial phones have different boot firmware, memory maps, interrupt controllers, display pipelines, touch controllers, power-management ICs, cameras, modems, GPUs, and vendor firmware.

QEMU `virt` also is not a complete smartphone emulator. Its purpose is early kernel development and automated tests.

## Status

The first ARM64 serial-console kernel implementation is committed. The QEMU CI smoke test is the current verification step.
