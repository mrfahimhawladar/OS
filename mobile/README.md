# Fahim OS Mobile

**Project goal:** build a general-purpose smartphone operating system. Fahim OS is not a ROM made only for the POCO X6 Neo, and the core project is not tied to Xiaomi or any one phone.

## 1. Development strategy

Develop a portable OS core first, then add hardware-specific ports separately.

The first engineering target is a **virtual ARM64 reference machine (QEMU `virt` )**. This is not a commercial smartphone emulator; it lets us implement and test the ARM64 kernel, memory, exceptions, timer, scheduler, storage and input interfaces without depending on one phone's firmware.

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
- The kernel prints the discovered RAM region over the serial console

The [successful ARM64 QEMU run](https://github.com/mrfahimhawladar/OS/actions/runs/42929915828) cross-compiled the ELF, booted it in QEMU, checked the RAM base and size, and uploaded the kernel artifact.

### Next kernel milestones

1. Parse reserved-memory and kernel/DTB reservations into an explicit reserved-region map
2. Add exception vectors and safe exception reporting
3. Add the GIC and generic timer
4. Implement a physical page allocator that only uses verified free RAM
5. Add MMU/page tables, then scheduler and context switching
6. Add IPC, VFS, and a RAM filesystem
7. Add generic input, display, and storage driver interfaces

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

**Verified:** ARM64 entry assembly, bootstrap stack, FDT-based PL011 console initialization, FDT RAM discovery, and QEMU serial boot test.

**Not implemented:** reserved-memory exclusions, page allocator, MMU, interrupts, timer, scheduler, IPC, filesystem, graphics, lock screen, home screen, applications, and physical-phone drivers.

The reported RAM map is informational only. The kernel does not allocate memory from it yet.

The existing root-level x86 files and `preview.html` are a separate learning prototype. They are not Fahim OS Mobile.
