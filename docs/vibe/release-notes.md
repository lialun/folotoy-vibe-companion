**English** · [简体中文](release-notes.zh_CN.md)

# Vibe Companion v1.0.0

## New in this release

Voice input now uses Wi-Fi and BLE together when both authorized links are
available. The computer merges audio by recording and sequence, discards duplicate
copies, and can continue over a surviving link without restarting input.

Single-link recordings also gain bounded recovery: the device retains a short
compressed history, retries temporary send pressure, and repairs missing blocks
requested by the computer. Link selection resists brief jitter. Trailing speech
is drained before a queued Enter action; full disconnection or sleep stops input
and never automatically starts a new recording.

Recovery has finite memory, retry and latency limits. Sustained outages can still
lose audio; this release does not promise lossless reception in every environment.

## Included

- Voice input and Enter, Agent task alerts, quota/weather rotation, Pomodoro,
  Internet radio, appearance and everyday settings in the companion app.
- Seven built-in notification integrations, quota collection for Codex, Claude
  Code and OpenCode Go, plus local custom-Agent notification/quota callbacks.
- One device row across Bluetooth and Wi-Fi discovery. Connect from the computer
  and confirm on the device; confirm computer handovers on the device too.
- Computer-decoded radio over either link, with voice interruption and resume.
- Universal Apple Silicon/Intel macOS app, bilingual installation guides,
  redistribution notices and a verified merged firmware image.

## Install and update

Update both the desktop app and device firmware to v1.0.0 to enable the complete
voice recovery protocol. Older peers keep their previous voice behavior.
See `INSTALL.txt` or `INSTALL.zh_CN.txt` for setup and permissions. Keep the app
running and the computer awake while using the device. An update may require
refreshing the app's existing Accessibility authorization.

The merged `FoloToy-AI-Passport-full.bin` is written at `0x0` and can reset saved
settings and pairing. To preserve settings, developers should use the compatible
application-partition upgrade described in the firmware-layout documentation.
Verify the downloaded files against `SHA256SUMS.txt`.

Packages use free ad-hoc signing and are not Apple notarized. The current desktop
client supports macOS. One device authorizes one computer at a time. Online radio
and quota data are fetched by the computer; quota availability depends on the
Agent's existing login, plan and supplied fields.

## Known issue

Internet radio can still stutter when delivery changes to BLE and retransmissions
accumulate. This release does not include a radio scheduling fix.

## Build from source

Activate ESP-IDF 5.5.3 and run `./tools/validate.sh` for the complete gate, including
host tests, an isolated firmware build and merged-image verification.
`./tools/validate.sh --firmware` produces the verified
`build/FoloToy-AI-Passport-full.bin` without the host gate. On macOS, run
`bash macos/build.sh` for the universal app, native tests, DMG and ZIP.

## Validation scope

The voice implementation passed the complete local gate and native Mac tests.
On-device automated checks passed 27/27 assertions across single Wi-Fi, single
BLE, injected packet losses, temporary send failures, dual-link repair, full
outage and recovery, and temporary-cache release. Eight continuity recordings
received all 1,750 captured audio blocks without a missing playout block.
The user confirmed long/short real speech and immediate Enter on the installed
app and normal firmware. Sleep/wake and resumed input were manually accepted
during the dual-link development cycle; cleanup also has native regression tests.
These checks cover their tested conditions, not every possible radio environment.

The 1.0.0 release wrapper is built again by GitHub Actions before publication.
Long weak-signal sessions, battery/current measurements, clean-system and physical
Apple Silicon/minimum-macOS testing, and multiple physical computers/devices
remain outside the completed acceptance scope. The known radio issue above remains.
