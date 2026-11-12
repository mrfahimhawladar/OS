# Fahim OS Lock Screen

This directory contains the first complete visual/UI layer for the Fahim OS lock screen and a standalone preview.

## Files

- **preview.html**: self-contained, responsive lock-screen interface preview. It has a live clock/date, status indicators, notification surfaces, bottom shortcuts, swipe-up gesture, PIN bouncer, emergency-information panel, and preview unlock transition.
- **README.md**: integration boundary and implementation notes.

## Preview controls

- Tap the lock screen or swipe upward to open the PIN bouncer.
- Enter any six digits to see the preview-unlocked transition.
- Use the delete key to remove a digit.
- The flashlight and camera shortcuts demonstrate their UI states.
- Press Escape to close an open panel or return from the bouncer.

## Important security boundary

preview.html is a UI preview only. It does not read or verify a real device credential, operate the flashlight, launch the camera, place emergency calls, or unlock Android. Those actions must be connected to the native Android Keyguard/SystemUI services. Do not use this preview as a replacement for Android's secure Keyguard.

## Native integration target

Use the official AOSP android17-release implementation as the source of truth for device-level behavior. The focused source collector in /.github/workflows/collect-keyguard-source.yml retrieves the Keyguard-facing SystemUI source and its first-level shared libraries. It deliberately does not sync the full AOSP tree or the unrelated SystemUI notification/Quick Settings modules.

The native implementation must keep credential verification, biometric trust, emergency calling, lockout policy, falsing/touch security, doze/AOD, notification privacy, and keyguard lifecycle under the platform Keyguard services. The visual preview is a design and interaction reference until those platform interfaces are wired.

## Current status

- [x] Visual lock-screen layout and live time/date.
- [x] Notification presentation surface.
- [x] Flashlight and camera shortcut states.
- [x] Swipe/tap transition into PIN UI.
- [x] Numeric keypad, digit indicator, delete and cancel interactions.
- [x] Emergency and biometric integration placeholders.
- [ ] Native SystemUI view/controller wiring.
- [ ] Real device credential and biometric integration.
- [ ] Notification, battery, network and carrier data binding.
- [ ] AOSP build and device validation.
