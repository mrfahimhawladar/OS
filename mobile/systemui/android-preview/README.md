# Fahim OS Native Lock Screen Preview for Android

This is an installable Android viewer for the existing Fahim OS native C++ Lock Screen renderer. It compiles and runs:

- `runtime/src/LockScreenRenderer.cpp`
- `runtime/src/Compositor.cpp`
- `runtime/src/SessionGate.cpp`

No HTML/CSS lock-screen recreation or screenshot is displayed inside the app. The JNI bridge asks the same C++ renderer to draw a 440x900 ARGB frame, then Android displays that pixel buffer.

## What can be previewed

- Lock Screen with time/date, status indicators, notification cards and shortcuts.
- PIN bouncer and keypad; swipe up from the lock view to open it, and tap keypad keys to preview entered PIN dots.
- Incorrect PIN visual state.
- Lockout visual state.
- Flashlight-on visual state.

The clock/date and battery percentage are read live from Android. The notification/network values are illustrative renderer inputs, not connected system notification or telephony services.

## Build

    gradle --no-daemon -p mobile/systemui/android-preview assembleDebug

Requires JDK 17, Android SDK platform 35, Android NDK 27.2.12479018 and CMake 3.22.1.

## Device install

Install `app/build/outputs/apk/debug/app-debug.apk` on an arm64 Android device. Allow the file manager/browser to install unknown apps when Android asks, then install and open **Fahim OS Native Lock Screen**.

## Important boundary

This is a live Android app wrapper for previewing the actual native renderer, not the real operating-system Lock Screen. It cannot replace Android Keyguard, intercept the power-button lock lifecycle, authenticate the device PIN, or secure/block Android applications. PIN entry/incorrect/lockout modes are visual design states only. The app does not ask for or store the device PIN. No credentials are checked or accepted.
