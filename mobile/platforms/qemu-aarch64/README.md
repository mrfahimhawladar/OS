# QEMU ARM64 reference platform

**Role:** first virtual development environment for Fahim OS.

This is a generic ARM64 virtual machine, not a POCO/Xiaomi phone.

## Virtual hardware

The QEMU virt machine provides ARM64 CPU execution, RAM and generated DTB, GICv3 (selected explicitly in CI), ARM architected timer, PL011 serial, and VirtIO devices for later storage, network, display, and input work.

The CI boot command uses -machine virt,gic-version=3 so it does not depend on QEMU's default GIC selection. QEMU bare-metal boot places the generated DTB at 0x40000000; the kernel ELF is linked at 0x40200000. Device register regions are read from the DTB. See the [QEMU virt documentation](https://www.qemu.org/docs/master/system/arm/virt).

## Current verification

The GitHub Actions workflow builds and boots the ARM64 kernel in QEMU, verifies the PL011 console and memory map, confirms EL1 exception vector setup, discovers the GICv3 Distributor and Redistributor regions from the generated Device Tree, and runs a controlled BRK exception test.

[See the ARM64 QEMU workflow](https://github.com/mrfahimhawladar/OS/actions/workflows/arm64-kernel.yml).

## What this platform does not prove

QEMU virt is not a complete smartphone emulator. A successful boot does not prove support for any real smartphone; commercial phones have different boot firmware, memory maps, interrupt controllers, display pipelines, touch controllers, power-management ICs, cameras, modems, GPUs, and vendor firmware.
