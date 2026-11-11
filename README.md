# Fahim OS

Fahim OS is intended to become a **smartphone operating system**. The active development direction is ARM64 mobile, with the POCO X6 Neo 5G (India variant) as the first target candidate.

## Active project: Fahim OS Mobile

The mobile project is at the device research and architecture stage. The device tree, firmware, vendor components, kernel package, and India-variant compatibility must be audited before a ROM can be built or tested.

Start here:

- [Fahim OS Mobile architecture and roadmap](mobile/README.md)
- [POCO X6 Neo 5G target audit](mobile/targets/gold/README.md)
- [Machine-readable target record](mobile/targets/gold/device.json)
- [Safe read-only Termux device-info collector](mobile/tools/collect-target-info.sh)

**Current limitation:** no Fahim OS mobile ROM has been built, boot-tested, or made flash-ready. The bootloader is reported locked. Do not flash any image during the current research stage.

## Separate learning project: x86 prototype

The root-level `kernel.cpp`, `boot.s`, `linker.ld`, `Makefile`, and `grub.cfg` are a separate 32-bit x86 hobby-kernel experiment. It demonstrates a GRUB boot entry, C++ kernel, VGA text output, and a basic PS/2 command shell.

This x86 prototype is **not** the kernel, boot image, or installer for Fahim OS Mobile. Its ISO can be previewed in a browser emulator, but that emulates an x86 PC and does not boot the phone OS on Android hardware.

### x86 prototype preview

The `preview.html` page loads the v86 browser emulator. To use it, first enable GitHub Pages with **Settings → Pages → Source → GitHub Actions**, then open:

https://mrfahimhawladar.github.io/OS/

Download the `fahim-os-iso` artifact from the latest successful **Build Fahim OS** workflow, extract the ZIP, and select the ISO in the preview page.

## Project boundaries

- Mobile OS work lives under `mobile/`.
- The x86 learning prototype remains at repository root for now.
- Android ROM builds require a suitably provisioned 64-bit Linux build machine; the small x86 ISO GitHub Actions workflow is not an Android ROM builder.
- No proprietary firmware or vendor blobs are intentionally committed to this repository.
