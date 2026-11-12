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
- `runtime/src/CredentialBackend.cpp`: adapter to a trusted protected-credential service. It returns unavailable when there is no backend; it never accepts a preview PIN or stores credentials itself.
- `runtime/src/KernelSessionControl.cpp`: AArch64 SVC bridge from verifier-approved LockScreenCore state to the kernel's task-role session gate.
- `lockscreen/native/`: bounded PIN state machine and escalating in-memory lockout.

## Build and test

    cmake -S mobile/systemui -B build/systemui
    cmake --build build/systemui --parallel
    ctest --test-dir build/systemui --output-on-failure

## Hardware integration status

The framebuffer adapter works once a platform display driver supplies a mapped framebuffer and its actual format/stride. Platform controller drivers implement `RawTouchDevice`; the input pipeline calibrates and normalizes their samples before routing. The input path still needs a real QEMU virtio-input or phone touchscreen controller driver. This repository currently has no QEMU virtio-gpu/virtio-input driver, phone display driver, touchscreen controller, or device-specific framebuffer mapping, so these components are not yet connected to physical or QEMU display/input hardware.

The renderer and controller can now register the native bitmap as a LOCKSCREEN compositor surface, consume normalized input events, and synchronize the LockScreenCore state to the kernel SVC gate. Host tests exercise this complete software path with a test-only credential verifier and mock SVC transport. The QEMU kernel separately tests the locked scheduler path using a synthetic LOCKSCREEN-role EL0 task. The current credential adapter is only an interface to a trusted storage/TEE service; no production credential store, hardware-backed secret, or persistent lockout backend exists. Missing backends fail closed.

**This is not yet a boot-active Lock Screen, Android image, phone ROM, or flashable build.** It is the next testable software layer to integrate with the restored ARM64 kernel and future board drivers.

The kernel session gate starts locked and filters scheduler task roles. The native bridge uses SVC 8/9 to read or change that state. The kernel permits changes only from a task assigned the LOCKSCREEN role by trusted kernel code. Host tests inject a mock transport; the AArch64 syscall path still needs a boot-created Lock Screen process to exercise on QEMU/device.

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
