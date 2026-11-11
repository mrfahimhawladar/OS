# Third-party notices

## arm-gic 0.10.0

- Project: ArmFirmwareCrates / TrustedFirmware.org, Arm Generic Interrupt Controller driver
- Source: https://github.com/ArmFirmwareCrates/arm-gic
- Version: 0.10.0 (pinned in mobile/kernel/rust/Cargo.toml)
- License: dual-licensed MIT OR Apache-2.0
- Used by: the ARM64 kernel's no_std GICv3 bridge in mobile/kernel/rust/
- Scope: GICv3 Distributor, Redistributor and EL1 CPU-interface initialization, interrupt acknowledge, priority handling and EOI.

Fahim OS consumes the upstream crate as a Cargo dependency; the upstream driver source is not copied or forked into this repository. The upstream project's licensing texts and notices are available at the source URL above. The bridge code and Fahim OS integration are separate from the third-party driver.

The upstream crate's direct dependencies are arm-sysregs (EL1 feature), bitflags, safe-mmio, thiserror (default features disabled), and zerocopy (derive feature). Cargo resolves their transitive dependencies during the build.

## arm-generic-timer 0.3.1

- Project: ArmFirmwareCrates / TrustedFirmware.org, Arm Generic Timer driver
- Source: https://github.com/ArmFirmwareCrates/arm-generic-timer
- Version: 0.3.1 (pinned in mobile/kernel/rust/Cargo.toml)
- License: dual-licensed MIT OR Apache-2.0
- Used by: the ARM64 kernel's no_std Rust bridge in mobile/kernel/rust/
- Scope: EL1 Physical Timer (CNTP register access) through the upstream PhysicalTimer and generic Timer abstractions.

Fahim OS consumes the upstream crate as a Cargo dependency; its source is not copied or forked into this repository. The upstream project provides MIT and Apache-2.0 license texts in its source repository. Only the small OS integration bridge is maintained here.

The el1 feature enables EL1 system-register access. Default embedded-hal features are disabled because the kernel does not need those optional interfaces.


## frame-alloc 0.1.0

- Project: Paul Sandner, frame_alloc
- Source: https://github.com/PaulSandner/frame_alloc
- Release source branch commit: ae68fdc7ada347686859ba632c94ba1a97446190
- Version: 0.1.0 (pinned in mobile/kernel/rust/Cargo.toml)
- License: MIT
- Used by: the ARM64 kernel's no_std physical-page allocator bridge
- Scope: 4 KiB base-frame allocation, contiguous buddy blocks, boot usable-range registration and allocator statistics.

Fahim OS consumes the published crate; its allocator implementation is not copied into this repository. The crate has no runtime dependencies and does not require a heap. The upstream project marks its 0.1 API as unstable. Its own kernel-demo currently targets x86-64; the ARM64 QEMU port and identity-map provenance adapter are Fahim OS integration work and are validated in the Fahim OS workflow. This integration rejects initialization when SCTLR_EL1.M is set until a page-table-aware mapping provider is implemented.


## aarch64-paging 0.12.3

- Project: aarch64-paging Authors (Ard Biesheuvel and Andrew Walbran)
- Source: https://github.com/google/aarch64-paging
- Upstream commit: d2b1247113a235de5884ecc27dc0d2c0388054ca
- Version: 0.12.3 (pinned in mobile/kernel/rust/Cargo.toml)
- License: dual-licensed MIT OR Apache-2.0
- Used by: the ARM64 kernel's EL1 VMSA page-table builder
- Scope: 4 KiB stage-1 page tables, EL1&0 translation regime, mapping descriptor flags and TTBR activation.

Fahim OS uses the upstream Mapping API with a custom no-heap Translation implementation backed by frame-alloc. The crate source is not copied into this repository. The upstream README states that this is not an officially supported Google product. Fahim OS's QEMU validation is independent and limited to its single-core identity-map reference configuration.


## AArch64 context-switching reference: mini-rtos

- Project: KNCn23 / mini-rtos
- Source: https://github.com/KNCn23/mini-rtos
- Referenced files: kernel/task.h, kernel/sched.c, kernel/context_switch.S
- Used by: the initial Fahim OS AArch64 round-robin context-switch implementation in mobile/kernel/core/scheduler.c and mobile/kernel/arch/arm64/boot/context_switch.S
- License information: the upstream README states MIT; a standalone LICENSE file was not present in the inspected upstream default-branch tree.
- Scope: AArch64 callee-saved register/SP context layout, first-task stack setup, simple round-robin task selection, and register save/restore assembly.

Fahim OS adapts this small context-switch/scheduler foundation to its own EL0 trap frames and ASID-managed TTBR0 address spaces. It does not import the unrelated mini-rtos drivers or task data model. The syscall table and TTBR0 integration are Fahim OS code.


## Goblin ELF64 parser 0.10.7

- Project: m4b / goblin
- Source: https://github.com/m4b/goblin
- Version: 0.10.7 (pinned in mobile/kernel/rust/Cargo.toml)
- License: MIT
- Used by: mobile/kernel/rust/src/elf_loader.rs
- Scope: upstream ELF64 header, program-header structures and constants. Cargo defaults are disabled; only the ELF64 feature is enabled so the kernel uses the raw ELF structures without Goblin's standard-library or allocation-backed parser.

Fahim OS owns the bounded no-heap ET_EXEC validation, PT_LOAD mapping, permission policy, BSS zeroing, address-space integration and rollback logic. Goblin supplies the ELF format definitions; the upstream crate source is not copied into the repository.
