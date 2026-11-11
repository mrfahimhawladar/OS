# Fahim OS — Device-independent architecture

## Product definition

Fahim OS is a general-purpose smartphone operating system project. It is **not** a ROM made only for the POCO X6 Neo, and the project is not limited to Xiaomi devices.

The core OS must be designed independently of any one phone. Hardware-specific support belongs in separate board/device ports.

## Initial development platform

The first engineering target is a **virtual ARM64 reference platform** (QEMU `virt`), not a specific commercial smartphone.

Why:
- It gives the kernel a stable, documented ARM64 environment before phone-specific firmware and drivers are involved.
- Core CPU, memory, exception, timer, interrupt, storage, and input work can be tested without risking a physical phone.
- The same kernel architecture can later gain board-support packages for physical devices.

QEMU `virt` is a virtual development machine; it is not a complete emulation of a modern smartphone. Modem, camera ISP, fingerprint, vendor GPU, charging, and proprietary power hardware require separate device ports and cannot be assumed to work in the reference VM.

## Proposed system layers

```text
Fahim Applications
        |
Fahim UI / Shell / Window & Compositor Services
        |
System Services (settings, packages, permissions, connectivity APIs)
        |
User Runtime + IPC + VFS + Device Manager
        |
Fahim Kernel (ARM64)
        |
Architecture Layer + Generic Driver Interfaces
        |
Board Support Package (QEMU first; phone ports later)
        |
Hardware / Virtual Hardware
```

## Language policy

Use a language because a subsystem needs it, not to reach a target language count.

| Area | Initial language |
|---|---|
| ARM64 exception/entry/context-switch code | Assembly |
| Kernel core, memory, scheduler, interrupt and driver core | C |
| Higher-level native system services | C++ |
| Memory-safe services/drivers after the base is stable | Rust (later) |
| UI and applications | To be selected after the graphics/runtime interface is defined |
| Build and test tools | Make/CMake, shell, Python |

The first kernel milestone should stay small and testable. Do not begin with modem, camera, GPU, or cellular telephony drivers.

## Kernel architecture proposal

Start with a small **modular monolithic kernel**. Kernel subsystems communicate through explicit interfaces, while remaining in one privileged address space initially. This is a practical first design for a small team and allows driver work to begin without introducing a full microkernel IPC boundary immediately.

First kernel milestones:
1. ARM64 entry and exception vectors
2. Serial console and panic path
3. Device Tree parsing
4. Physical memory allocator and MMU/page tables
5. Generic interrupt controller and timer
6. Preemptive scheduler and threads
7. Kernel heap, synchronization, and IPC
8. VFS and a simple RAM filesystem
9. Generic input/display/storage driver interfaces

This is a proposed architecture, not already implemented code.

## Hardware independence

The kernel core must not hard-code phone model names or SoC registers. Architecture-specific code belongs under `arch/arm64/`; virtual hardware drivers under the QEMU platform; real phone support belongs under `boards/<vendor>/<device>/`.

A device port must supply its boot/firmware contract, memory map, interrupt routing, clocks, pin control, display/touch, storage, power, and other device-specific details. Proprietary firmware and vendor blobs must be handled separately and only where redistribution and licensing allow.

## Current boundaries

- The root directory remains the old 32-bit x86 teaching prototype.
- `mobile/` is the active general smartphone OS project.
- `mobile/targets/gold/` is an optional future POCO X6 Neo research port, not the main project target.
- No Fahim OS mobile kernel or ROM has been built or boot-tested yet.
