# Fahim OS Secure Credential Backend

## Components

- credential_ta/credential_ta.c: OP-TEE Trusted Application. PIN processing, verifier key use, credential record and lockout policy execute inside Secure World.
- provisioner_ta/provisioner_ta.c: separate signed Trusted Application. It accepts an enrollment request without a PIN parameter, opens the Secure World TUI PTA, collects PIN and confirmation there, then calls the Credential TA as an authenticated Trusted Application.
- platform/optee/fahim_tui_pta.c: OP-TEE core pseudo-TA overlay. Only the provisioner TA UUID can call it; it never accepts PIN bytes from the REE.
- runtime/src/TeeCredentialBackend.cpp: also exposes TeeCredentialService::enroll_initial_pin(), a TEEC client call that sends no PIN, key events or framebuffer to the TEE.
- include/fahim_credential_protocol.h: shared GlobalPlatform TEE Client/Trusted Application command protocol.
- runtime/src/TeeCredentialBackend.cpp: C++ adapter and optional GlobalPlatform TEEC client transport.
- runtime/src/CredentialBackend.cpp: bridges trusted result and secure retry deadline into LockScreenCore.

## Credential format and verification

The TA never persists the PIN. During enrollment it creates a random 256-bit salt, generates a non-exportable HMAC-SHA256 pepper key inside the TEE, and stores only:

- format/version and PIN length;
- the random salt;
- HMAC-SHA256(TEE pepper, salt || PIN length || PIN);
- failed-attempt count and lockout level;
- secure retry deadline and last-observed secure time.

The pepper key is a TEE persistent object in TEE_STORAGE_PRIVATE_RPMB. A record corruption, storage error, unavailable secure clock, or failed platform capability check is treated as unavailable; there is no plaintext, REE-file, or in-memory credential fallback.

PINs are numeric, 4–16 digits. Use at least six digits on a production device. Because a PIN has low entropy, the TEE enforces a persistent online rate limit: five failures trigger an escalating wait (30, 60, 120, 240, 480, 960, 1,920 seconds, then up to one hour). A successful PIN resets the failure counter and escalation level. A new five-attempt window begins after the secure lockout deadline expires.

## Hardware requirements and fail-closed policy

The TA uses TEE_STORAGE_PRIVATE_RPMB and checks the OP-TEE implementation properties before allowing query, enrollment or verification:

- gpd.tee.trustedStorage.antiRollback.protectionLevel >= 1000 (hardware TEE anti-rollback, normally RPMB with the platform key provisioned);
- gpd.tee.systemTime.protectionLevel >= 1000 (TEE-controlled system clock, protected from REE software).

OP-TEE's QEMU vexpress-qemu_armv8a reference target normally does not provide both of these production properties. On that target, the TA deliberately returns UNAVAILABLE; a QEMU TA build is only compile/link verification. Do not lower these checks to make QEMU appear to authenticate.

The platform must provision and enable RPMB, its authentication key, a TEE-controlled system clock, OP-TEE secure boot/TA signature verification, and a trusted-storage implementation before using this backend for production PINs.

## Enrollment authorization boundary

FAHIM_CRED_CMD_ENROLL is accepted only from a TEE Trusted Application whose UUID is pinned in credential_ta.c (28e3b2d4-4a20-4f91-9a11-464148494d50). The separate Provisioner TA uses TEE_OpenTASession to call the Credential TA, so the Credential TA sees TEE_LOGIN_TRUSTED_APP and the exact provisioner UUID. A normal-world TEEC client cannot call ENROLL directly.

The Provisioner TA itself accepts only a command with an output status structure; it never accepts PIN input. It opens the Fahim TUI pseudo-TA, which authenticates the provisioner UUID and obtains PIN digits from a Secure World HAL hook into core-private memory. The provisioner collects two entries, checks them in constant time, and sends the first PIN only over a secure TA-to-TA call to the Credential TA. The normal-world launcher receives status only.

The Trusted UI pseudo-TA and Provisioner TA source are now in this repository and are included in CI builds. However, the board-specific strong secure display/touch HAL is not implemented yet. The PTA's weak default begin/read hooks return TEE_ERROR_NOT_SUPPORTED, so first-time enrollment remains intentionally unavailable until a supported board integrates secure display/input drivers. Do not replace this with normal-world UI or expose ENROLL to a public client.

## Build

Build the Trusted Application with the OP-TEE developer kit:

    make -C third_party/optee_os \
      CFG_ARM64_core=y \
      CFG_USER_TA_TARGETS=ta_arm64 \
      CROSS_COMPILE=aarch64-linux-gnu- \
      CROSS_COMPILE_core=aarch64-linux-gnu- \
      CROSS_COMPILE_ta_arm64=aarch64-linux-gnu- \
      O=out/arm PLATFORM=vexpress-qemu_armv8a ta_dev_kit

    make -C mobile/systemui/tee/credential_ta \
      TA_DEV_KIT_DIR="$PWD/third_party/optee_os/out/arm/export-ta_arm64" \
      CROSS_COMPILE=aarch64-linux-gnu- O=build

The resulting TA package is mobile/systemui/tee/credential_ta/build/9f4d1e72-6a0b-4a31-9b60-464148494d43.ta. The CI uses OP-TEE's bundled default development TA signing key (`default_ta.pem`) only to prove the package can be built and signed. The uploaded artifact is named fahim-os-credential-ta-dev-signed. It is NOT production-trusted and must be re-signed with the device owner's protected TA signing key before installation on a production device.

Build the normal-world runtime with a platform-installed OP-TEE client development package:

    cmake -S mobile/systemui -B build/systemui \
      -DFAHIM_ENABLE_OPTEE_CLIENT=ON
    cmake --build build/systemui --parallel

The option is OFF by default. With it OFF, OpteeCredentialTransport is unavailable and every authentication attempt fails closed.

## Verification scope

- Host CI tests exercise the C++ transport/backend mapping with a deterministic fake TEE transport. They verify enrollment/duplicate enrollment, PIN acceptance, secure retry deadline propagation across a newly created LockScreenCore, and fail-closed behavior when the transport is unavailable.
- OP-TEE CI cross-builds the actual Trusted Application against a pinned upstream OP-TEE developer kit and inspects the generated ARM64 ELF and signed TA package.
- CI does not emulate an RPMB key, TEE hardware anti-rollback counter, TEE-protected system clock, secure boot signing key or Trusted UI. Therefore it does not prove that a real device can authenticate or enroll credentials.

## Trusted Provisioning TA and Trusted UI bridge

Build both TAs and the core PTA overlay through the OP-TEE workflow:

    make -C mobile/systemui/tee/credential_ta \
      TA_DEV_KIT_DIR="$PWD/third_party/optee_os/out/arm/export-ta_arm64" \
      CROSS_COMPILE=aarch64-linux-gnu- O=build

    make -C mobile/systemui/tee/provisioner_ta \
      TA_DEV_KIT_DIR="$PWD/third_party/optee_os/out/arm/export-ta_arm64" \
      CROSS_COMPILE=aarch64-linux-gnu- O=build

The OP-TEE CI overlays platform/optee/fahim_tui_pta.c into core/pta and builds the pinned OP-TEE ARM64 developer kit before signing both TAs. The combined CI artifact is fahim-os-secure-enrollment-dev-signed. It is development-signed, not production-trusted.

The native launcher calls TeeCredentialService::enroll_initial_pin() with no PIN argument. If the secure TUI provider is absent, the operation returns Unavailable and the credential remains unenrolled. The Trusted UI board hook contract is documented in platform/optee/README.md.
