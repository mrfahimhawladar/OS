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

See [QEMU ARM64 platform plan](platforms/qemu-aarch64/README.md) and [kernel source and roadmap](kernel/README.md).

### Verified milestone 1: ARM64 boot + serial console

- AArch64 freestanding entry point and bootstrap stack
- C kernel entry reached successfully
- Device Tree (FDT) parsed
- PL011 UART discovered from the Device Tree and initialized
- Fahim OS ARM64 serial banner printed in QEMU

### Verified milestone 2: FDT RAM discovery

- Enabled Device Tree memory nodes are read using the parent bus cell counts
- RAM address and size tuples are decoded with overflow and malformed-range checks
- QEMU CI confirms the 512 MiB RAM range: base `0x40000000`, size `0x20000000`

### Verified milestone 3: reserved memory and usable RAM

The kernel now combines the FDT reserve map, static `/reserved-memory` child ranges, the kernel image boundaries, and the DTB blob into one sorted/merged reservation map. It subtracts those reservations from RAM and reports the remaining usable ranges.

The QEMU test confirms:
- RAM: `0x40000000` + `0x20000000` (512 MiB)
- Reserved low-memory window: `0x40000000` + `0x00100000`
- Kernel image reservation: `0x40200000` + `0x00031000`
- Usable ranges begin at `0x40100000` and `0x40231000`

[Open the successful QEMU build and memory-layout test](https://github.com/mrfahimhawladar/OS/actions/runs/42930479064).

### Next kernel milestones

1. Add exception vectors and safe exception reporting
2. Add the GIC and generic timer
3. Implement a physical page allocator using page-aligned usable ranges
4. Add MMU/page tables
5. Add scheduler, threads, and context switching
6. Add IPC, VFS, and a RAM filesystem
7. Add generic input, display, and storage driver interfaces

The current usable map is diagnostic only. It is not yet consumed by an allocator. Dynamic `/reserved-memory` nodes that specify `size` without a fixed `reg` range are rejected safely until a placement allocator exists.

QEMU `virt` is a virtual reference board, not a full phone emulator. A QEMU boot does not prove camera, modem, GPU, fingerprint, cellular, battery, or touchscreen support on any real phone.

## 4. Separate hardware ports

See [Board Support Ports](boards/README.md).

A real phone needs its own boot contract, memory map, Device Tree, interrupt/power/clock configuration, and hardware drivers. These must live outside the generic kernel core.

The [POCO X6 Neo `gold` research record](targets/gold/README.md) remains an optional example of future device-port research. It is not a requirement for core development.

## 5. Build environment

The Android phone can be used to edit files, inspect source, manage GitHub, and run lightweight scripts. It is not a suitable full build host for a large native mobile OS tree.

The first ARM64 kernel is small enough for a cross-compilation CI job. A full native mobile OS and later hardware ports will need a suitably provisioned Linux build host or cloud build machine.

## 6. Safety

- Core OS development and virtual-machine testing come before physical-device flashing.
- No image is to be flashed to any phone during the current phase.
- A physical-device port must have verified hardware identity, compatible boot chain, tested images, full backups, and a recovery route.
- Do not redistribute proprietary firmware or vendor binaries without permission.

## 7. Current status

**Verified:** ARM64 entry assembly, bootstrap stack, FDT-based PL011 console initialization, RAM discovery, reserved-range merging/subtraction, and QEMU memory-layout validation.

**Not implemented:** page allocator, MMU, interrupts, timer, scheduler, IPC, filesystem, graphics, lock screen, home screen, applications, and physical-phone drivers.

The existing root-level x86 files and `preview.html` are a separate learning prototype. They are not Fahim OS Mobile.

## 8. Lock Screen / SystemUI runtime foundation

The ARM64 kernel baseline has been restored to this repository and its existing QEMU workflow was re-run successfully. It currently verifies the generic QEMU boot, MMU/address-space path and scheduler; it does not yet provide a graphics or touchscreen device driver.

The native SystemUI layer now includes:

- A bounds/stride-checked linear framebuffer adapter for XRGB8888 and RGB565.
- A normalized touch/pointer router which keeps input on the Lock Screen while locked and recognizes swipe-up, camera and flashlight actions.
- A fixed-capacity alpha compositor with z-order and an explicit session gate. Application surfaces and application input are denied until the native lock state is Unlocked.
- A protected-credential backend adapter which fails closed when no trusted backend is connected.
- CMake tests for pixel layout/clipping, touch routing, locked-surface suppression, unlock transition and missing-credential-backend behavior.

Build and run the SystemUI tests:

    cmake -S mobile/systemui -B build/systemui
    cmake --build build/systemui --parallel
    ctest --test-dir build/systemui --output-on-failure

**Still not connected:** QEMU virtio-gpu/simple-framebuffer mapping, physical-phone display controller, touchscreen/virtio-input driver, an OS window/session manager, kernel-enforced process/session lock gate, TEE/secure credential storage and persistent anti-brute-force state. The framebuffer adapter expects a real platform driver to supply a validated mapped framebuffer; the credential adapter expects a trusted service to implement atomic verification and persistent policy. Both are integration contracts, not fake hardware.

The Lock Screen is **not boot-active** and the repository does not yet contain a flashable smartphone image or ROM.

The ARM64 scheduler now has a fail-closed session gate: locked at boot, application-role tasks are not scheduled while locked, and only kernel-assigned Lock Screen/Emergency roles may run. The native runtime includes a bridge to the kernel SVC lock-state interface. A trusted process loader must still create the Lock Screen task with the LOCKSCREEN role before normal application processes are enabled.
