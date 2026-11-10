# Fahim OS

A small, experimental hobby operating system built around a freestanding **C++ kernel** for 32-bit x86.

This repository contains an early bootable OS foundation. It is not yet a general-purpose operating system.

## Current milestone

- GRUB Multiboot 1 boot entry
- 32-bit x86 assembly startup code
- Freestanding C++ kernel
- VGA text-mode console
- Legacy PS/2 keyboard input (polling mode)
- Basic command shell
- Browser-based x86 emulator preview page powered by v86

## Shell commands

Type a command and press Enter:

- `help` — list available commands
- `clear` — clear the text screen
- `about` — show project information
- `version` — show the current milestone
- `echo <text>` — print text

The current keyboard driver reads legacy PS/2 set-1 make codes. It supports basic US keyboard characters, Shift, Enter, and Backspace. It is an early polling driver; interrupt-driven input, Caps Lock, USB keyboard support, and international layouts are not implemented yet.

## Preview Fahim OS in a browser

The repository includes `preview.html`, which runs the actual bootable ISO inside the v86 x86 PC emulator.

### One-time GitHub Pages setup

1. Open the repository **Settings**.
2. Open **Pages** under **Code and automation**.
3. Under **Build and deployment**, set **Source** to **GitHub Actions**.
4. Open the **Actions** tab and let **Deploy Fahim OS Preview** finish successfully.

After deployment, open:

```
https://mrfahimhawladar.github.io/OS/
```

### Boot your ISO on Android

1. Open the **Actions** tab and select the latest successful **Build Fahim OS** run.
2. Download the `fahim-os-iso` artifact ZIP.
3. Extract the ZIP using your Android file manager. You need the `fahim-os.iso` file inside it.
4. Open the preview website above in Chrome.
5. Select `fahim-os.iso` and press **Boot Fahim OS**.
6. Use the touchscreen text input to send commands such as `help`, `about`, or `version`.

The ISO is read locally by your browser and is not uploaded to the website. The emulator library and BIOS images are loaded from the official v86 website, so an internet connection is required. Mobile performance varies by device and browser. This is a browser-based x86 PC emulation, not native Android execution.

## Build the ISO locally

The build uses a Linux environment with these packages:

- `g++-multilib`
- `grub-pc-bin`
- `grub-common`
- `xorriso`
- `mtools`
- `make`

Then run:

```sh
make
```

The ISO will be created at:

```
dist/fahim-os.iso
```

To remove generated files:

```sh
make clean
```

## Build it from an Android phone

You do not need to compile the kernel on the phone. Every push to `main` triggers GitHub Actions to build the ISO on a GitHub-hosted Ubuntu runner. Open the latest successful **Build Fahim OS** run and download the `fahim-os-iso` artifact.

## Project structure

- `boot.s` — Multiboot header and assembly entry point
- `kernel.cpp` — freestanding C++ kernel, console, keyboard, and shell
- `linker.ld` — kernel memory layout
- `grub.cfg` — GRUB boot menu
- `Makefile` — local build instructions
- `preview.html` — browser emulator interface
- `.github/workflows/build.yml` — automated ISO build
- `.github/workflows/pages.yml` — GitHub Pages deployment

## Roadmap

1. ~~Keyboard input and a basic command line~~
2. CPU exceptions and interrupt handling
3. Physical and virtual memory management
4. Timer and task scheduling
5. Storage and filesystem support
6. User programs and a graphical interface

## Important

This is an educational operating-system project. It currently targets legacy 32-bit x86 BIOS/GRUB boot through Multiboot 1. Keyboard input currently depends on legacy PS/2 controller emulation. It is not an Android app and does not replace the phone's operating system.
