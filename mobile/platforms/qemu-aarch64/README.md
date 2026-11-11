# QEMU ARM64 reference platform

**Role:** first virtual development environment for Fahim OS.

This target is a generic ARM64 virtual machine, not a POCO/Xiaomi phone. It is intended to test the portable kernel and generic drivers before any physical device port.

## Expected virtual hardware

The QEMU `virt` machine can provide standard virtualized components such as:
- ARMv8/ARM64 CPU execution
- RAM and a Device Tree description
- Generic interrupt controller (GIC), depending on QEMU configuration
- ARM generic timer
- PL011 serial console
- VirtIO devices for later block, network, display, and input experiments

Exact device versions and command-line parameters will be recorded in the build scripts when the first bootable kernel is implemented.

## Initial boot milestone

The first goal is deliberately small:
1. Cross-compile an AArch64 freestanding kernel.
2. Start it in QEMU's `virt` machine.
3. Read and validate the Device Tree.
4. Initialize a serial console.
5. Print a Fahim OS kernel banner and report detected RAM.
6. Halt safely.

Only after this works should the project add MMU/page allocation, timer interrupts, scheduling, and virtual input/display.

## What this platform does not prove

A successful QEMU boot does not prove support for any real smartphone. Commercial phones have different boot firmware, memory maps, interrupt controllers, display pipelines, touch controllers, power-management ICs, cameras, modems, GPUs, and vendor firmware.

## Status

Planning/documentation only. No ARM64 kernel binary or QEMU boot image has been built yet.
