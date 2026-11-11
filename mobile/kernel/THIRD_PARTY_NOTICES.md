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
