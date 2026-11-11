# Fahim OS Mobile

**Project goal:** build a general-purpose smartphone operating system. Fahim OS is not a ROM made only for the POCO X6 Neo, and the core project is not tied to Xiaomi or any one phone.

## 1. Development strategy

Develop a portable OS core first, then add hardware-specific ports separately.

The first engineering target is a **virtual ARM64 reference machine (QEMU `virt`)**. This is not a commercial smartphone emulator; it lets us implement and test the ARM64 kernel, memory, exceptions, timer, scheduler, storage and input interfaces without depending on one phone's firmware.

After the core stabilizes, individual smartphone Board Support Packages (BSPs) can be added. A POCO X6 Neo / `gold` port is optional future work, not the definition of Fahim OS.

## 2. Architecture

See [the architecture overview](architecture/overview.md).

| Layer | Planned technology | Responsibility |
|---|---|---|
| CPU entry / exception / context switch | ARM64 Assembly | Architecture-specific operations |
| Kernel | C | Memory, exceptions, scheduling, interrupt and driver core |
| Native system services | C++ initially; Rust where useful later | Higher-level system components |
| UI and applications | To be selected after graphics/runtime design | Lock screen, home screen, settings and apps |
| Build and test | Cross-compiler, linker, Make/CMake, shell, Python | Reproducible builds and automated checks |

The proposed starting point is a small modular monolithic ARM64 kernel. The language list is a design choice, not a required language count.

## 3. First development platform: QEMU ARM64

See [QEMU ARM64 platform plan](platforms/qemu-aarch64/README.md) and [kernel roadmap](kernel/README.md).

First boot target:
1. AArch64 freestanding kernel entry
2. Serial console and panic output
3. Device Tree (FDT) parsing
4. Detect available memory
5. Safe halt

Then add page allocation/MMU, interrupts/timer, scheduler, IPC, VFS, and generic input/display/storage drivers.

QEMU `virt` is a virtual reference board, not a full phone emulator. A QEMU boot does not prove camera, modem, GPU, fingerprint, cellular, battery, or touchscreen support on any real phone.

## 4. Separate hardware ports

See [Board Support Ports](boards/README.md).

A real phone needs its own boot contract, memory map, Device Tree, interrupt/power/clock configuration, and hardware drivers. These must live outside the generic kernel core.

The [POCO X6 Neo `gold` research record](targets/gold/README.md) remains an optional example of future device-port research. It is not a requirement for core development.

## 5. Build environment

The Android phone can be used to edit files, inspect source, manage GitHub, and run lightweight scripts. It is not a suitable full build host for a large native mobile OS tree.

For the ARM64 kernel, the first build can use a suitably provisioned Linux AArch64 cross-compilation environment. A QEMU test job may run in CI once the bootable kernel exists. A full Android source build is not part of this custom-kernel plan.

## 6. Safety

- Core OS development and virtual-machine testing come before physical-device flashing.
- No image is to be flashed to any phone during the current phase.
- A physical-device port must have verified hardware identity, compatible boot chain, tested images, full backups, and a recovery route.
- Do not redistribute proprietary firmware or vendor binaries without permission.

## 7. Current status

**Completed now:** architecture documents, ARM64 kernel roadmap, QEMU reference-platform plan, board-port policy, optional POCO research record.

**Not yet implemented:** ARM64 kernel source, cross-build, QEMU boot image, graphics, lock screen, home screen, or phone drivers.

The existing root-level x86 files and `preview.html` are a separate learning prototype. They are not Fahim OS Mobile.
