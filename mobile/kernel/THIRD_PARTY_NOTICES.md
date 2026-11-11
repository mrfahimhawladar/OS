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
