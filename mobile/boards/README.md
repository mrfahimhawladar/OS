# Fahim OS board support ports

Fahim OS core development is device-independent. Real phone support is added later through separate Board Support Packages (BSPs).

A board port may contain:
- Device/SoC identity and supported revisions
- Boot firmware and kernel-entry contract
- Device Tree source and overlays
- Memory map and reserved memory
- Interrupt controller, clocks, pin control, and power domains
- Display, touchscreen, storage, USB, Wi-Fi, audio, camera, modem, sensors, and battery integration
- Firmware/vendor component references with licensing notes
- Known limitations and a tested recovery path

## Port policy

1. Never assume two phones with similar names share an identical board.
2. Do not add model-specific registers or hacks to generic kernel code.
3. Do not redistribute proprietary firmware or vendor binaries without permission.
4. Mark a port as experimental until its boot path and essential hardware are tested.
5. Do not flash a physical phone without an unlocked/authorized boot path, verified images, full backups, and a recovery plan.

## Candidate ports

- `../targets/gold/` — optional research record for POCO X6 Neo / gold family. It is not the main target for Fahim OS.
