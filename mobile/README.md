# Fahim OS Mobile

**Active direction:** a smartphone operating system, not a desktop OS.

The first hardware target is the POCO X6 Neo 5G (India variant). This is a bring-up project: no Fahim OS mobile image has been built, boot-tested, or made flash-ready yet.

## 1. Architecture decision

Fahim OS Mobile will start from an Android-compatible open-source platform (AOSP/LineageOS family) and add Fahim's own system identity, interface, services, and applications.

We are not replacing the Linux kernel with a brand-new kernel at the first milestone. Reusing the Android hardware abstraction and device support is the realistic route for a phone with a MediaTek SoC, modem, camera stack, GPU, fingerprint sensor, and power-management firmware.

Planned layers:

| Layer | Planned technology | Responsibility |
|---|---|---|
| Boot and CPU-specific code | ARM64 Assembly, existing boot chain | Device boot and low-level entry |
| Kernel | Linux, primarily C | Memory, scheduling, drivers |
| Native services | C / C++ / Rust | System daemons and native components |
| Android framework | Java / Kotlin | Application and system services |
| Fahim System UI | Kotlin, framework overlays, native components where needed | Lock screen, home screen, quick settings |
| Build and validation | Repo, Make, Soong, Python, shell | Source sync, image builds, tests |

This is a layered mobile OS plan. Languages will be added when a component needs them; we will not add languages just to increase the count.

## 2. Initial target

See [the gold target record](targets/gold/README.md) and [machine-readable device record](targets/gold/device.json).

Current target facts:

- Device family: POCO X6 Neo 5G
- User's model: 2312FRAFDI (India)
- Device codename reported by the device: `gold`
- SoC platform: MediaTek MT6833 / Dimensity 6080
- CPU architecture: ARM64
- User-reported installed kernel: 6.6.118-android15
- User-reported firmware build: OS3.0.5.0.VNQINXM
- User-reported bootloader state: locked

The exact regional hardware/vendor compatibility is not yet confirmed.

## 3. Device support research

A public community device tree exists at:

- Device tree: https://github.com/Dhterech/android_device_xiaomi_gold
- Candidate branch: `lineage-23.2`

That tree declares `TARGET_ARCH := arm64`, `TARGET_BOARD_PLATFORM := mt6833`, and a 6.6 kernel target. Its README reports working Wi-Fi, mobile data/IMS, tethering, USB, fast charging, camera, fingerprint, IR, and sensors on the maintainer's test device.

**Important limitation:** the maintainer says it was tested on the China `gold` variant and that other variants may or may not work. VoLTE, NFC, and the ultrawide camera were not tested. The branch is a research starting point, not proof that the user's India variant is safe or compatible.

Its board configuration also forces a prebuilt kernel and prebuilt DTB/DTBO/modules for the initial build. Therefore, it is not yet a verified from-source kernel build for this phone.

## 4. Build-environment reality

A full Android source build cannot be done on this Android phone or on the small x86 ISO workflow in this repository.

The current official AOSP setup requirements specify a 64-bit x86 Linux workstation, at least 400 GB free disk space (250 GB checkout plus 150 GB build space), and at least 64 GB RAM:
https://source.android.com/docs/setup/start/requirements

We can continue source research, write configuration, and run lightweight checks from GitHub using the phone. A real ROM build will need access to a suitably provisioned Linux build machine or cloud runner. The existing GitHub Actions job only builds the tiny x86 learning prototype; it is not an Android ROM builder.

## 5. Safety and device rules

- Do not flash any image while the bootloader is locked.
- Do not try to bypass Xiaomi's bootloader authorization.
- Do not use a ROM built for a different codename or regional variant.
- Do not overwrite or remove the stock firmware as a development shortcut.
- Keep proprietary vendor files out of this public repository unless redistribution is explicitly permitted.
- Do not call a build "ready" until it passes image validation, emulator/device boot tests, and a documented recovery path.

The user has reported that the bootloader is locked and that development is limited to an Android phone. Those constraints remain part of this plan.

## 6. Work stages

1. **Target identity audit** — compare the live device properties with the community tree's supported variants.
2. **Source and firmware audit** — identify the exact device tree, kernel/prebuilt kernel, vendor components, and firmware baseline.
3. **Product configuration** — create a Fahim OS product target without changing device-specific low-level configuration blindly.
4. **First image build** — build in a suitable Linux environment and run static image checks.
5. **Emulator and UI work** — bring up the system image and develop Fahim branding, lock screen, home screen, and settings.
6. **Hardware validation** — only after bootloader authorization, backups, verified images, and a recovery plan.

## Separate x86 experiment

The repository's `kernel.cpp`, `boot.s`, `Makefile`, and `preview.html` are a separate 32-bit x86 learning prototype. It is not the kernel or installer for Fahim OS Mobile.
