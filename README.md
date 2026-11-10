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

## Shell commands

Type a command and press Enter:

- `help` — list available commands
- `clear` — clear the text screen
- `about` — show project information
- `version` — show the current milestone
- `echo <text>` — print text

The current keyboard driver reads legacy PS/2 set-1 make codes. It supports basic US keyboard characters, Shift, Enter, and Backspace. It is an early polling driver; interrupt-driven input, Caps Lock, USB keyboard support, and international layouts are not implemented yet.

## Build the ISO

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

You do not need to compile the kernel on the phone. After a push to the `main` branch:

1. Open this repository on GitHub.
2. Open the **Actions** tab.
3. Select **Build Fahim OS** and open the latest successful run.
4. Download the **fahim-os-iso** artifact.

The workflow builds the ISO on a GitHub-hosted Ubuntu runner. The artifact is a ZIP download containing the ISO.

## Project structure

- `boot.s` — Multiboot header and assembly entry point
- `kernel.cpp` — freestanding C++ kernel, console, keyboard, and shell
- `linker.ld` — kernel memory layout
- `grub.cfg` — GRUB boot menu
- `Makefile` — local build instructions
- `.github/workflows/build.yml` — automated ISO build

## Roadmap

1. ~~Keyboard input and a basic command line~~
2. CPU exceptions and interrupt handling
3. Physical and virtual memory management
4. Timer and task scheduling
5. Storage and filesystem support
6. User programs and a graphical interface

## Important

This is an educational operating-system project. It currently targets legacy 32-bit x86 BIOS/GRUB boot through Multiboot 1. Keyboard input currently depends on legacy PS/2 controller emulation. It is not an Android app and does not replace the phone's operating system.
