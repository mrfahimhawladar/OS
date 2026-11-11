# QEMU ARM64 reference platform

**Role:** first virtual development environment for Fahim OS.

This target is a generic ARM64 virtual machine, not a POCO/Xiaomi phone. It tests the portable kernel and generic drivers before any physical device port.

## Virtual hardware

The QEMU `virt` machine provides standard virtual components such as:
- ARMv8/ARM64 CPU execution
- RAM and a generated Device Tree Blob (DTB)
- Generic interrupt controller (GIC)
- ARM architected timer
- PL011 serial console
- VirtIO devices for later block, network, display, and input experiments

QEMU's bare-metal boot mode places the generated DTB at the start of RAM (0x40000000). In this setup QEMU reserves 1 MiB for the DTB, so the kernel ELF is linked at 0x40200000. Other device locations are discovered from the DTB rather than hard-coded. See the [QEMU virt documentation](https://www.qemu.org/docs/master/system/arm/virt).

## Verified first kernel milestone

The GitHub Actions workflow **Build Fahim OS ARM64 Kernel #6** successfully:
1. Cross-compiled the freestanding ARM64 ELF
2. Loaded and executed the kernel in QEMU `virt`
3. Parsed the generated Device Tree
4. Located and initialized the PL011 UART
5. Verified the Fahim OS serial boot banner

[See the successful CI run](https://github.com/mrfahimhawladar/OS/actions/runs/42929151504).

## Verified serial output

```text
FAHIM OS ARM64
Kernel entry: OK
Device Tree: parsed
PL011 UART: initialized from Device Tree
Platform: QEMU virt (ARM64 reference target)
```

## What this platform does not prove

A successful QEMU boot does not prove support for any real smartphone. Commercial phones have different boot firmware, memory maps, interrupt controllers, display pipelines, touch controllers, power-management ICs, cameras, modems, GPUs, and vendor firmware.

QEMU `virt` is not a complete smartphone emulator. Its purpose is early kernel development and automated tests.

## Status

The first ARM64 serial-console kernel has booted in QEMU. The next milestone is memory-map parsing, exception vectors, and interrupt/timer setup.
