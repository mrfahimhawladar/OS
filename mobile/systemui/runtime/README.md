# Fahim OS SystemUI runtime foundation

This is a device-independent native runtime foundation for the lock screen. It deliberately separates testable UI/security policy from board-specific hardware drivers.

## Included components

- `runtime/src/FramebufferDisplay.cpp`: checked linear framebuffer adapter for XRGB8888 and RGB565. It validates the provided mapped-memory length, dimensions and stride, supports clipping, tracks damage and offers a cache/flush callback.
- `runtime/src/InputRouter.cpp`: normalized pointer/touch event router. While locked, events are exclusively routed to Lock Screen; it recognizes upward swipe, camera and flashlight shortcut taps.
- `runtime/src/Compositor.cpp`: fixed-capacity alpha compositor with z-order and an OS session gate. Application surfaces are omitted whenever the lock state is not Unlocked.
- `runtime/src/SessionGate.cpp`: fail-closed presentation/input policy. Only the Lock Screen, public system overlays and explicitly activated emergency dialer may be presented while locked.
- `runtime/src/CredentialBackend.cpp`: adapter to a trusted protected-credential service. It returns unavailable when there is no backend; it never accepts a preview PIN or stores credentials itself.\n- `runtime/src/KernelSessionControl.cpp`: AArch64 SVC bridge from verifier-approved LockScreenCore state to the kernel's task-role session gate.
- `lockscreen/native/`: bounded PIN state machine and escalating in-memory lockout.

## Build and test

    cmake -S mobile/systemui -B build/systemui
    cmake --build build/systemui --parallel
    ctest --test-dir build/systemui --output-on-failure

## Hardware integration status

The framebuffer adapter works once a platform display driver supplies a mapped framebuffer and its actual format/stride. Platform controller drivers implement `RawTouchDevice`; the input pipeline calibrates and normalizes their samples before routing. The input path still needs a real QEMU virtio-input or phone touchscreen controller driver. This repository currently has no QEMU virtio-gpu/virtio-input driver, phone display driver, touchscreen controller, or device-specific framebuffer mapping, so these components are not yet connected to physical or QEMU display/input hardware.

The compositor and session gate are functional policy/rendering components and have host tests. They are not yet linked to an OS window manager/process manager or kernel lock gate. The current credential adapter is only an interface to a trusted storage/TEE service; no production credential store, hardware-backed secret, or persistent lockout backend exists. Missing backends fail closed.

**This is not yet a boot-active Lock Screen, Android image, phone ROM, or flashable build.** It is the next testable software layer to integrate with the restored ARM64 kernel and future board drivers.

The kernel session gate starts locked and filters scheduler task roles. The native bridge uses SVC 8/9 to read or change that state. The kernel permits changes only from a task assigned the LOCKSCREEN role by trusted kernel code. Host tests inject a mock transport; the AArch64 syscall path still needs a boot-created Lock Screen process to exercise on QEMU/device.
