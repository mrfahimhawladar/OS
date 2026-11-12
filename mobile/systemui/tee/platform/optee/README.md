# Fahim Trusted UI OP-TEE platform port

This folder contains the OP-TEE core pseudo-TA overlay and the required board HAL contract.

## Security boundary

The fahim_tui_pta.c pseudo-TA is compiled into the OP-TEE core. Only the signed Fahim Provisioner TA UUID (28e3b2d4-4a20-4f91-9a11-464148494d50) can open or invoke it. The PTA does not accept PIN bytes as command input. It asks the platform HAL to draw a Secure World prompt and collect digits directly into a core-private buffer, then copies the result to the trusted Provisioner TA's private memory.

The normal-world provisioning client invokes the provisioner with a status output only. It does not pass a PIN, key events, a framebuffer, or a normal-world callback.

## Board implementation requirements

A supported board must provide strong implementations of all three hooks in fahim_tui_platform_hal.h:

- fahim_tui_platform_begin(): take exclusive ownership of the physical display and touch/input path, switch to an authenticated Secure World screen, and show a clear "Set up Fahim OS PIN" prompt. It must require explicit user presence/confirmation before allowing entry.
- fahim_tui_platform_read_pin(): receive digit events directly from the secure touch/key input driver, render masked input inside the trusted display, and write only the final numeric PIN to the supplied Secure World buffer. It must not use REE input, an Android/Linux UI, shared memory event queues, or a callback into the normal world.
- fahim_tui_platform_end(): erase TUI buffers, release secure input state, and hand display/input ownership back safely. The weak default returns TEE_ERROR_NOT_SUPPORTED, not success.

The status-display hook should render success, mismatch, cancellation or unavailable status while the secure screen still owns the display.

## Default behavior

The weak HAL implementation is intentionally unavailable. QEMU vexpress and ordinary Linux CI can compile the PTA and both TAs, but they do not gain trusted input or display support. Until a board supplies secure drivers and strong hooks, BEGIN returns TEE_ERROR_NOT_SUPPORTED and PIN enrollment cannot proceed.

Do not replace the weak defaults with code that reads normal-world touchscreen events or receives the PIN as a TEEC parameter. That would be a look-alike UI, not Trusted UI.

## Build integration

The OP-TEE workflow copies fahim_tui_pta.c, fahim_tui_platform_hal.h and fahim_tui_protocol.h into the pinned OP-TEE source tree and registers the PTA in core/pta/sub.mk before building the ARM64 developer kit. A real board port must integrate the same PTA source and its strong HAL hooks into the actual OP-TEE core image used by the device boot chain.

## GlobalPlatform TUI conformance status

The current Fahim TUI PTA is a project-specific, fail-closed command bridge and board HAL contract. It is not yet a complete implementation of the GlobalPlatform TEE Trusted User Interface Low-level API (GPD_SPE_055), nor a claim of Trusted UI PP-Module compliance. A production board port must implement the secure display/input ownership and event path required by the applicable GlobalPlatform TUI API/profile, or provide a reviewed mapping from that API to these hooks. At least one user input or output peripheral must be permanently or temporarily controlled by the TEE during the trusted interaction.

Reference:
- GlobalPlatform TEE Trusted User Interface Low-level API v1.0.1 (GPD_SPE_055): https://globalplatform.org/specs-library/globalplatform-technology-tee-trusted-user-interface-low-level-api-v1-0-1/
- Trusted User Interface PP-Module v1.0 (GPT_SPE_142): https://globalplatform.org/specs-library/
