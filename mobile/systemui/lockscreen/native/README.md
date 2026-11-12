# Fahim OS native lock-screen core

This is the first native security/state component for the lock screen. It is independent of Android framework classes and graphical toolkits so it can be used by Fahim OS system services after the UI/input layers exist.

## Implemented

- Explicit Locked, Authenticating, Lockout, and Unlocked states.
- Bounded numeric credential input (4–16 digits).
- Immediate clearing of the transient input buffer after verification, lock, and destruction.
- A verifier interface; no default PIN, password, hash, or bypass is embedded in the UI component.
- Five consecutive rejected PIN submissions trigger monotonic-clock lockout: 30s, 60s, 120s, 240s, then a 300s cap.
- A temporarily unavailable credential backend fails closed and does not unlock the device.
- Host-buildable tests with an explicitly test-only fake verifier.

## Build and test

    cmake -S . -B build
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

## Security and integration boundary

This component is not, by itself, a running device lock screen. The production CredentialVerifier must be implemented by a trusted OS-owned credential service with protected credential storage, authenticated IPC, persistent lockout state, and hardware-backed verification where available. The window manager/compositor and scheduler/session manager must enforce that locked sessions cannot expose user applications. Only the trusted lock-screen process may receive the required input while locked.

Fahim OS's QEMU ARM64 kernel does not yet have a framebuffer/display, input device, compositor/window manager, credential store, or production lock-screen session gate. Until those exist and are wired to this core, this is a tested native subsystem, not a boot-active lock screen.

## Android 17 reference

The collected AOSP Keyguard code is a design/reference source, not a standalone library. KeyguardService extends Android Service and coordinates with Android internal Binder, WindowManager, PowerManager, SystemUI dependency injection, and framework services. It cannot be dropped into the current bare-metal kernel without that Android platform stack.
