# Fahim OS Secure Credential Backend

## Components

- credential_ta/credential_ta.c: OP-TEE Trusted Application. PIN processing, verifier key use, credential record and lockout policy execute inside Secure World.
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

FAHIM_CRED_CMD_ENROLL is accepted only from a TEE Trusted Application whose UUID is pinned in credential_ta.c (28e3b2d4-4a20-4f91-9a11-464148494d50). A normal-world TEEC client uses TEEC_LOGIN_PUBLIC, so it can query/verify but cannot enroll or replace the first credential.

The trusted provisioning TA and its Trusted UI/physical-presence path are not yet implemented in this repository. Until that signed Secure-World provisioner is built and installed by the device owner, first-time PIN enrollment is intentionally unavailable. Do not remove the identity check or expose ENROLL to a public normal-world client to bypass this limitation.

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
