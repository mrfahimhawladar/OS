# Target audit: POCO X6 Neo 5G (gold)

**Status: candidate only — India variant not yet proven compatible.**

## Identity currently recorded

| Property | Value | Evidence |
|---|---|---|
| Marketing name | POCO X6 Neo 5G | User's device |
| Model | `2312FRAFDI` | User's device |
| Market | India | Model suffix / user device context |
| Device codename | `gold` | User-reported product/vendor properties; Device Info listing |
| SoC platform | MediaTek `mt6833` | Device tree and hardware database |
| SoC | Dimensity 6080 | Device hardware |
| CPU architecture | `arm64` | Device hardware / supported ABI |
| Kernel | `6.6.118-android15` | User-reported `uname -r` |
| Firmware | `OS3.0.5.0.VNQINXM` | User-reported installed build |
| Bootloader | Locked | User-reported state |

The hardware database lists model 2312FRAFDI, codename gold, MT6833, ARM64 ABI, and Dimensity 6080:
https://www.deviceinfo.app/devices/poco/poco-x6-neo/

## Community device-tree candidate

Repository: https://github.com/Dhterech/android_device_xiaomi_gold  
Branch: `lineage-23.2`

The tree README says:
- It targets Redmi Note 13 5G / Redmi Note 13R Pro / POCO X6 Neo 5G (`gold/iron`).
- The maintainer uses the tree as a daily driver.
- Wi-Fi, mobile data/IMS, tethering, USB, fast charging, camera, fingerprint, IR, and other sensors are listed as working.
- A particular Wi-Fi hotspot + Wi-Fi client combination can fail.
- Offline charging is listed as not working.
- VoLTE, NFC, and ultrawide camera were not tested.
- The maintainer says testing was on China `gold`; global `iron` and other variants may or may not work.

Do not treat this as certification for model 2312FRAFDI / the India firmware.

## What the board configuration tells us

The candidate `BoardConfig.mk` sets:
- `TARGET_ARCH := arm64`
- `TARGET_CPU_VARIANT := cortex-a76`
- `TARGET_BOARD_PLATFORM := mt6833`
- `TARGET_KERNEL_VERSION := 6.6`
- `TARGET_KERNEL_CONFIG := gki_defconfig gold.config`

It also sets `TARGET_FORCE_PREBUILT_KERNEL := true` and points at a prebuilt kernel, DTB/DTBO, and vendor modules. This is a significant caveat: the first ROM bring-up relies on a prebuilt kernel package rather than proving a reproducible kernel build from source.

The same tree's product file reports model `2312DRAABG`, so we must not blindly copy that product identity to the Indian model. A proper India-variant product property set must be confirmed first.

Useful source files:
- https://github.com/Dhterech/android_device_xiaomi_gold/blob/lineage-23.2/README.md
- https://github.com/Dhterech/android_device_xiaomi_gold/blob/lineage-23.2/BoardConfig.mk
- https://github.com/Dhterech/android_device_xiaomi_gold/blob/lineage-23.2/lineage_gold.mk

## Next read-only identity check

Use the included `mobile/tools/collect-target-info.sh` in Termux. It prints device/product/platform/build properties and the kernel version. It deliberately does not request IMEI, serial number, phone number, SIM identifiers, or account data.

The result is needed before selecting a device tree or creating a flashable image. Do not flash anything as part of this audit.
