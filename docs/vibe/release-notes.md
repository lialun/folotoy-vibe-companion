**English** · [简体中文](release-notes.zh_CN.md)

# Vibe Companion v0.9.1

## New in this release

This update improves the Mac app's sleep and wake lifecycle. System sleep blocks
new device work immediately, releases held shortcut keys, and stops Bluetooth
scanning/connections, LAN retries, audio and quota polling. Network/audio cleanup
runs on its owning queue instead of blocking the sleep notification handler.

System wake restores communication independently of display state. Manual
disconnect intent survives sleep and automatic permission reloads; stale timers
and callbacks cannot reconnect or disconnect a newer session. Interrupted quota
queries become eligible after wake while completed polls keep their normal
interval and genuine failures keep their backoff. Pending radio-list writes are
cancelled before their connection closes.

The device firmware's functional behavior is unchanged from v0.9.0. Existing
v0.9.0 devices can use the new desktop app without reflashing or re-pairing.

## Included

- Voice input and Enter, Agent task alerts, quota/weather rotation, Pomodoro,
  Internet radio, appearance and everyday settings in the companion app.
- Seven built-in notification integrations, quota collection for Codex, Claude
  Code and OpenCode Go, plus local custom-Agent notification/quota callbacks.
- Bluetooth and Wi-Fi discovery merged into one device row. Start connections
  on the computer and confirm on the device; both links may remain attached.
- Computer-decoded radio over either link, with voice interruption and resume.
- Universal Apple Silicon/Intel macOS app, bilingual installation guides,
  redistribution notices and a verified merged firmware image.

## Install and update

For an existing v0.9.0 setup, replace the desktop app and reopen it. New setups
can use both packages from this release. See `INSTALL.txt` or
`INSTALL.zh_CN.txt` for installation, permissions, input-method setup and
connection. Update existing Agent integrations where the app offers it; restart
or reload the Agent session and complete any trust prompt inside that Agent.
Quota availability depends on the logged-in plan and fields supplied by the Agent.

The merged `FoloToy-AI-Passport-full.bin` is written at `0x0` and can reset device
settings. Developers preserving settings should use the compatible application
partition workflow described in the source documentation. Verify `SHA256SUMS.txt`.

Packages use free ad-hoc signing and are not Apple notarized. Initial launch or
an update may require system approval and refreshed Accessibility authorization.

## Build from source

Activate ESP-IDF 5.5.3 and run `./tools/validate.sh` for host checks, the isolated
firmware build and merged-image verification. `./tools/validate.sh --firmware`
runs the firmware-only gate and produces `build/FoloToy-AI-Passport-full.bin`.
On macOS, run `bash macos/build.sh` for the universal app, native tests, DMG and ZIP.

## Validation scope

The committed implementation passed the complete repository/firmware gate and
native Mac regressions for sleep admission, repeated wake, deferred callbacks,
held-key release, permission-session preservation, quota-subprocess cancellation
and radio-list abort delivery. The universal app also passed packaged radio
codec/network tests. Publication is gated by the corresponding GitHub Actions
checks, firmware build and universal Mac build.

Real lid-close sleep/temperature behavior, clean-system installation, physical
Apple Silicon/minimum macOS and multiple physical computers/devices were not
verified for this update. Automated checks do not establish the cause of every
reported overheating incident or replace physical sleep acceptance. Earlier
functional and quota acceptance is recorded in v0.9.0 and v0.8.4.

The current desktop client supports macOS. One device authorizes one computer
at a time; a confirmed handover replaces that authorization. Keep the computer
awake and the app running. The computer fetches radio and online quota data.
