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

**Verified on the QEMU ARM64 reference target:** AArch64 boot/EL1 entry, exception vectors, FDT memory and PL011 discovery, reserved-memory exclusion, physical page allocator, GICv3 initialization, ARM Generic Timer IRQ delivery, EL1 MMU with TTBR1 high-half mapping, independent EL0 address spaces, SVC entry/return, single-core task scheduling, sleep/wakeup, WAITPID, and lower-EL fault isolation.

**Still not implemented:** general IPC/VFS/storage/network services, a boot-time process loader/session manager, QEMU/phone display and touchscreen drivers, a device-connected compositor, deployment of an OP-TEE Secure World on this OS boot chain, hardware RPMB and protected clock provisioning, the board-specific trusted display/touch HAL, production TA signing/trust anchor, and physical-phone board support.

The existing root-level x86 files and `preview.html` are a separate learning prototype. They are not Fahim OS Mobile.

## 8. Lock Screen / SystemUI runtime foundation

The ARM64 kernel baseline has been restored to this repository and its existing QEMU workflow was re-run successfully. It currently verifies the generic QEMU boot, MMU/address-space path and scheduler; it does not yet provide a graphics or touchscreen device driver.

The native SystemUI layer now includes:

- A bounds/stride-checked linear framebuffer adapter for XRGB8888 and RGB565.
- A raw-touch adapter contract, coordinate calibration/rotation, normalized-event queue and Lock Screen input routing.
- A native C++ Lock Screen renderer for clock/date/status, notification cards, shortcuts and the PIN bouncer/keypad.
- A Lock Screen controller that consumes normalized touch events, verifies PIN through an injected credential backend, and sends verifier-approved state to the kernel SVC gate.
- A fixed-capacity alpha compositor. The native Lock Screen renderer registers its bitmap as a LOCKSCREEN surface; application surfaces are filtered while the kernel session is locked.
- A GlobalPlatform OP-TEE client adapter plus an OP-TEE Trusted Application source for TEE-held HMAC pepper, salted PIN verifier, RPMB credential record and persistent anti-brute-force policy.
- A fail-closed credential service wrapper; host tests cover enrollment/verification result mapping, persistent lockout mapping and unavailable-TEE behavior.
- A separate pinned OP-TEE ARM64 Trusted Application build workflow; QEMU/CI compilation does not prove hardware RPMB or a secure clock.

Build and run the SystemUI tests:

    cmake -S mobile/systemui -B build/systemui
    cmake --build build/systemui --parallel
    ctest --test-dir build/systemui --output-on-failure

**Still not connected:** QEMU virtio-gpu/ramfb or phone display mapping, QEMU virtio-input or a phone touchscreen driver, a boot-time trusted process/session manager, OP-TEE Secure World integration into the boot chain, RPMB key/storage provisioning, a TEE-protected system clock, production TA signing/installation and the separate trusted provisioning TA/Trusted UI. `FAHIM_ENABLE_OPTEE_CLIENT` is OFF by default; without a running OP-TEE service the credential service returns unavailable and cannot unlock. The framebuffer and touchscreen APIs still require real platform drivers.

The scheduler gate starts locked. The QEMU self-test now creates a synthetic EL0 task assigned the kernel-only LOCKSCREEN role; it invokes SVC 9 to unlock, after which the four test application tasks are allowed to run. This validates the scheduler-level gate and SVC policy, not production PIN verification or boot-time service launch.

The native C++ renderer/controller path is host-tested and connects the touch queue, PIN UI, protected credential interface, kernel-session bridge and compositor surface. A TEEC enrollment client now calls the separate Provisioner TA without transmitting PIN data. The Provisioner TA and secure TUI pseudo-TA compile in CI, but the board-specific secure display/input HAL is intentionally unavailable by default. Therefore initial PIN enrollment still fails closed on current QEMU/CI and on any board without a real secure TUI driver. The Lock Screen is **not yet visibly boot-active** on QEMU or a phone, and no smartphone ROM or flashable image exists.

## Native System Shell implementation status

The native C++ System Shell now includes:
- Home screen with only implemented native system entries.
- Control Center backed by platform service calls; unavailable radio/display drivers are reported unavailable.
- Settings navigation and appearance/security/about entry points.
- Authenticator screen backed by a secure-vault interface; the Normal World receives only account metadata and generated codes, never TOTP secrets.
- About/device capability page.
- Kernel SYSTEM_SHELL scheduler role, blocked while the session is locked.

These are native OS runtime components compiled into the SystemUI library. They are not a booted phone UI yet: process manager/ELF loader, QEMU/POCO display and touchscreen drivers, live platform-service providers and a secure TOTP-vault TA remain unconnected. No Android Activity or screenshot is used as the operating-system implementation.
