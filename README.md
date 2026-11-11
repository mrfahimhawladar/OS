# Fahim OS

Fahim OS is a project to build a **general-purpose smartphone operating system**. It is not designed only for the POCO X6 Neo or any single manufacturer's phone.

## Active project: Fahim OS Mobile

The core OS is being designed independently of device-specific hardware. The first development platform is a virtual ARM64 reference machine (QEMU `virt`). Later, separate Board Support Packages can add support for individual real phones.

Start here:

- [Fahim OS Mobile architecture](mobile/architecture/overview.md)
- [ARM64 kernel roadmap](mobile/kernel/README.md)
- [QEMU ARM64 reference platform](mobile/platforms/qemu-aarch64/README.md)
- [Hardware board-port policy](mobile/boards/README.md)

The POCO X6 Neo / `gold` folder is only an optional future device-port research note. It is not the primary target.

**Current status:** architecture and platform planning are documented. An ARM64 kernel, QEMU boot image, graphical UI, and phone drivers have not yet been implemented or tested. No image is ready to flash.

## Separate learning project: x86 prototype

The root-level `kernel.cpp`, `boot.s`, `linker.ld`, `Makefile`, and `grub.cfg` are a separate 32-bit x86 learning prototype. It demonstrates a GRUB boot entry, C++ kernel, VGA text output, and a basic PS/2 command shell.

This x86 prototype is not the kernel, boot image, or installer for Fahim OS Mobile. Its ISO can be previewed in a browser emulator, but that emulates an x86 PC and does not boot Fahim OS on a phone.

### x86 prototype preview

The `preview.html` page loads the v86 browser emulator. To use it, enable GitHub Pages with **Settings → Pages → Source → GitHub Actions**, then open:

https://mrfahimhawladar.github.io/OS/

Download the `fahim-os-iso` artifact from the latest successful **Build Fahim OS x86 Prototype** workflow, extract the ZIP, and select the ISO in the preview page.

## Repository boundaries

- `mobile/` — active, device-independent smartphone OS project.
- `mobile/platforms/qemu-aarch64/` — first virtual ARM64 development target.
- `mobile/boards/` — future real-device ports.
- `mobile/targets/gold/` — optional POCO X6 Neo research only.
- Root-level x86 files — separate learning prototype.
- Proprietary firmware/vendor binaries are not committed unless redistribution is explicitly permitted.
