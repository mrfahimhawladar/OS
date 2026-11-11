# Optional future board-port research: POCO X6 Neo (gold)

**This is an optional hardware-port note, not the main target of Fahim OS.**

Fahim OS is being developed as a device-independent smartphone OS. The generic kernel and services will be developed first on the virtual ARM64 QEMU reference platform. A `gold` board port may be considered later, after the OS core is stable.

## Research identity

| Property | Recorded value |
|---|---|
| Marketing name | POCO X6 Neo 5G |
| Model | `2312FRAFDI` |
| Device codename | `gold` (user-reported) |
| SoC platform | MediaTek `mt6833` |
| SoC | Dimensity 6080 |
| CPU architecture | ARM64 |
| Firmware | `OS3.0.5.0.VNQINXM` (user-reported) |
| Bootloader | Locked (user-reported) |

These facts are a research note only. They do not change the overall Fahim OS target or roadmap.

## Community device-tree lead

Repository: https://github.com/Dhterech/android_device_xiaomi_gold  
Branch: `lineage-23.2`

The tree's own README says testing was performed on China `gold`; global `iron` or other variants may or may not work. It reports several hardware functions, but that is not certification for the India model. Its `BoardConfig.mk` also forces a prebuilt kernel and DTB/DTBO/modules for its bring-up.

This source may be useful later as a reference for a device port, but Fahim OS core must not depend on it.

## Port prerequisites

Before attempting any physical-device boot:
- Verify the exact board/hardware revision and firmware baseline.
- Audit boot chain, partition layout, Device Tree and required firmware.
- Determine what drivers and vendor components are available and legally redistributable.
- Build and test the generic OS independently first.
- Have an authorized bootloader path, full backup, validated images and a recovery plan.

The read-only [Termux target-info collector](../../tools/collect-target-info.sh) is optional and is not required for general Fahim OS development.

**No Fahim OS image is currently ready to flash to this or any other phone.**
