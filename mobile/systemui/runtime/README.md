# Fahim OS SystemUI runtime foundation

This is a device-independent native runtime foundation for the lock screen. It deliberately separates testable UI/security policy from board-specific hardware drivers.

## Included components

- `runtime/src/FramebufferDisplay.cpp`: checked linear framebuffer adapter for XRGB8888 and RGB565. It validates the provided mapped-memory length, dimensions and stride, supports clipping, tracks damage and offers a cache/flush callback.
- `runtime/src/LockScreenRenderer.cpp`: native ARGB lock-screen view, with live-value inputs for clock/date/status, notification cards, shortcuts, PIN bouncer, PIN dots, numeric keypad and unlock/cancel controls.
- `runtime/src/LockScreenController.cpp`: consumes normalized touch events, drives the native PIN state machine, handles lock-screen shortcuts, synchronizes verified state to the kernel SVC gate, and registers/removes the lock-screen surface in the compositor.
- `runtime/src/InputDevice.cpp`: raw touchscreen adapter interface, coordinate calibration/rotation, fixed-capacity event queue, and normalized-event delivery to the lock-screen controller.
- `runtime/src/InputRouter.cpp`: normalized pointer/touch event router. While locked, events are exclusively routed to Lock Screen; it recognizes upward swipe, camera and flashlight shortcut taps.
- `runtime/src/Compositor.cpp`: fixed-capacity alpha compositor with z-order and an OS session gate. Application surfaces are omitted whenever the lock state is not Unlocked.
- `runtime/src/SessionGate.cpp`: fail-closed presentation/input policy. Only the Lock Screen, public system overlays and explicitly activated emergency dialer may be presented while locked.
- `runtime/src/CredentialBackend.cpp` and `runtime/src/TeeCredentialBackend.cpp`: trusted credential adapter plus optional GlobalPlatform TEEC transport. It never accepts a preview PIN or stores credentials itself.
- `runtime/src/KernelSessionControl.cpp`: AArch64 SVC bridge from verifier-approved LockScreenCore state to the kernel's task-role session gate.
- `lockscreen/native/`: bounded PIN state machine and escalating in-memory lockout.

## Build and test

    cmake -S mobile/systemui -B build/systemui
    cmake --build build/systemui --parallel
    ctest --test-dir build/systemui --output-on-failure

## Hardware integration status

The framebuffer adapter works once a platform display driver supplies a mapped framebuffer and its actual format/stride. Platform controller drivers implement `RawTouchDevice`; the input pipeline calibrates and normalizes their samples before routing. The input path still needs a real QEMU virtio-input or phone touchscreen controller driver. This repository currently has no QEMU virtio-gpu/virtio-input driver, phone display driver, touchscreen controller, or device-specific framebuffer mapping, so these components are not yet connected to physical or QEMU display/input hardware.

The renderer and controller can now register the native bitmap as a LOCKSCREEN compositor surface, consume normalized input events, and synchronize the LockScreenCore state to the kernel SVC gate. Host tests exercise this complete software path with a test-only credential verifier and mock SVC transport. The QEMU kernel separately tests the locked scheduler path using a synthetic LOCKSCREEN-role EL0 task. The runtime now has an OP-TEE Trusted Application source, a GlobalPlatform TEEC transport and a fail-closed credential service. The TA source uses a TEE-held HMAC pepper, per-enrollment salt and RPMB-backed lockout state. No OP-TEE Secure World is deployed in the Fahim OS boot chain yet, and actual RPMB/secure-clock provisioning plus the trusted provisioning TA/Trusted UI are still missing. Without the TEE transport, authentication fails closed.

**This is not yet a boot-active Lock Screen, Android image, phone ROM, or flashable build.** It is the next testable software layer to integrate with the restored ARM64 kernel and future board drivers.

The kernel session gate starts locked and filters scheduler task roles. The native bridge uses SVC 8/9 to read or change that state. The kernel permits changes only from a task assigned the LOCKSCREEN role by trusted kernel code. Host tests exercise the C++ bridge with a mock transport. The QEMU kernel test separately runs a synthetic LOCKSCREEN-role EL0 payload which issues SVC 9 directly; it does not boot the C++ renderer/controller or verify a real credential.

## Native UI event loop

A trusted Lock Screen process can use the runtime as follows:

1. The board/QEMU input driver implements `RawTouchDevice`.
2. `TouchInputPipeline::pump_one()` reads one sample and normalizes/calibrates it.
3. `LockScreenController::pump_input()` consumes queued events and routes them to the bouncer/controller.
4. `LockScreenController::render_to_compositor()` renders the current lock-screen state and registers its surface as `SurfaceRole::LockScreen`.
5. `Compositor::compose()` draws only surfaces allowed by the kernel/session gate; application surfaces remain hidden while locked.

The caller must provide a valid framebuffer-backed `DisplayDevice`, ARGB scratch buffers and a trusted `CredentialVerifier`. The runtime does not fabricate a default PIN or silently unlock when credential services are absent.

## Still required for device activation

- A QEMU virtio-gpu/ramfb or board-specific display driver that maps a real framebuffer and invokes the cache/flush callback.
- A QEMU virtio-input or physical touchscreen driver implementing `RawTouchDevice`.
- A boot-time trusted process loader/session manager that creates the Lock Screen with the kernel-assigned `LOCKSCREEN` role before application tasks.
- A real protected credential backend (TEE/hardware-backed where available), enrollment, persistent anti-brute-force state and authenticated service IPC.
- Live clock, battery, carrier, network and notification services.

This gives the native software path and tested UI/controller integration. It does **not** yet put a visible Lock Screen on the QEMU monitor or any phone, because there is no connected display/input hardware driver or boot-launched Lock Screen service. No device ROM or flashable image is produced.

## Secure credential backend

The native runtime includes TeeCredentialBackend, TeeCredentialService and a GlobalPlatform TEEC client transport. A Lock Screen process constructs `TeeCredentialService` first, then constructs `LockScreenCore` with `service.verifier()`; the service owns the transport and backend for the verifier's lifetime. The matching OP-TEE Trusted Application lives in mobile/systemui/tee/credential_ta/. PIN derivation and verification run in Secure World using a per-device TEE-held HMAC-SHA256 pepper plus a random per-enrollment salt; the PIN itself is not persisted. The TA stores its verifier and failed-attempt/secure-deadline state in private RPMB Trusted Storage and refuses authentication unless the TEE reports hardware anti-rollback level 1000 and TEE-protected system-time level 1000. It fails closed on missing hardware properties or any storage/clock failure.

The FAHIM_ENABLE_OPTEE_CLIENT CMake option is OFF by default. Without an installed OP-TEE client library, the transport is unavailable and PIN verification returns unavailable rather than accepting a software fallback. The native CI uses a fake transport for adapter behavior tests; the separate OP-TEE workflow compiles the real TA against a pinned upstream developer kit.

Enrollment is intentionally more restricted than verification. The TA accepts enrollment only from a separately signed Secure-World provisioning TA with a pinned UUID. A normal-world TEEC_LOGIN_PUBLIC caller cannot enroll or replace a PIN. That provisioning TA and its Trusted UI/physical-presence path have not yet been implemented, so first-time production enrollment is not yet available. See mobile/systemui/tee/README.md for required platform setup and exact verification boundaries.

## Native renderer screenshots

The workflow `.github/workflows/lockscreen-core.yml` builds `native_lockscreen_snapshot` and runs it against the real `LockScreenRenderer.cpp` implementation. It emits 440x900 PNG snapshots for:
- `native-lockscreen.png`: renderer's locked state.
- `native-pin-bouncer.png`: renderer's PIN-entry UI state (zero digits entered).

These are host-rendered framebuffer outputs from the native C++ code, not the HTML preview. They are not screenshots captured from a booted QEMU/phone display, and they do not exercise real hardware or credential verification.

## Native OS System Shell

The native System Shell is built into fahim_systemui_runtime and is not an Android Activity or HTML mock-up:

- SystemShellRenderer renders Home, Control Center, Settings, Authenticator and About directly into an ARGB framebuffer.
- SystemShellController switches screens, handles normalized touch events and draws the active screen into the existing compositor.
- SystemPlatformServices is a platform-driver/service contract for Wi-Fi, Bluetooth, airplane mode, flashlight, brightness, theme, time and battery. The shell only reports an action as successful when the real backend returns success.
- AuthenticatorVault returns account labels and generated one-time codes only. It never exports TOTP seeds or shared secrets into the Normal World/SystemUI process. An unavailable secure vault produces an explicit unavailable/empty state.
- The TOTP code is expected to be generated within a trusted authenticator service; secret material is not processed in SystemUI.

Build and test the unified shell with:

    cmake -S mobile/systemui -B build/systemui
    cmake --build build/systemui --parallel
    ctest --test-dir build/systemui --output-on-failure

The shell compositor surface is assigned the Application role, so it is filtered by the session gate while the device is locked. The kernel scheduler now has a SYSTEM_SHELL task role that is not runnable while locked and can run after the trusted Lock Screen unlocks the session.

### OS activation boundary

The System Shell renderer, controller, touch routing, compositor integration and platform service interfaces are native OS components. They are not yet boot-launched on QEMU or a phone. The current kernel has no general process loader/session manager, no QEMU/phone display driver and no touchscreen controller driver. No production Wi-Fi/Bluetooth/brightness service backend or secure TOTP vault implementation is connected yet. Those services fail closed/offline rather than simulating working hardware.
