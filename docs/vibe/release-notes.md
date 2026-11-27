**English** · [简体中文](release-notes.zh_CN.md)

# Vibe Companion v1.0.2

**Automatic reconnection across computers, with more reliable Wi-Fi setup.**

Take Vibe Companion between home and work. Computers you have approved can reconnect automatically when the device becomes available. An active connection stays in control; to switch immediately, request a handover and confirm it on the device.

## What's new

- **Remember multiple computers:** store up to four independent computer authorizations. Approve each once instead of pairing again whenever you switch.
- **Reconnect when available:** after the current computer quits or its connection expires and the session is released, another approved computer can connect automatically.
- **Stay in control:** another computer will not automatically take over an active session. A manual request shows the computer name on the device; declining or cancelling keeps the original connection working.
- **Fix Wi-Fi setup failures:** improve cases where a phone joins the device hotspot but cannot load the setup page or network list. Bluetooth resources are temporarily released during setup and restored when setup completes or closes.
- **Show connection progress:** submitting Wi-Fi details immediately shows a connecting state, prevents duplicate submissions, and allows editing and retry after failure.
- **Clearer controls and labels:** connection confirmation has only Confirm and Cancel. The audio-driver status explicitly says BlackHole 2ch was detected, distinguishing driver detection from microphone selection.

## Install and upgrade

Update **both the desktop app and firmware to 1.0.2** for complete automatic reconnection across approved computers.

Unpair in the desktop app removes only that computer. Unpair on the device clears all computer authorizations. Both actions retain Wi-Fi settings.

Universal Apple Silicon/Intel DMG and ZIP packages are included. Follow `INSTALL.txt`. macOS may require refreshing the app's existing Accessibility permission after updating. Keep the computer awake and the app running during use.

Flash the merged `FoloToy-AI-Passport-full.bin` from `0x0`; this may reset Wi-Fi, device settings and pairing. Developers retaining settings should follow the repository firmware-layout guide for a compatible application-partition upgrade. Verify `SHA256SUMS.txt` after downloading.

## Build from source

Activate ESP-IDF 5.5.3 and run `./tools/validate.sh` for host tests, firmware build and merged-image verification. On macOS, `bash macos/build.sh` creates universal packages and runs native tests. GitHub Actions rebuilds the release tag and uploads packages after the release gates pass.
