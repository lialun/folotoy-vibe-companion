**English** · [简体中文](release-notes.zh_CN.md)

# Vibe Companion — release overview

A community firmware and macOS companion for FoloToy AI Passport. The version is
identified by the release tag and package filenames.

## Included

- Wireless hold-to-talk input through the device microphone, with a separate
  Enter action. Voice remains available while using Pomodoro or radio.
- Agent notifications for Codex, Claude Code, OpenCode, DeepSeek Harness, ZCode,
  Gemini CLI and Qwen Code, with per-integration event coverage. The first alert
  is immediate; additional callbacks for the same Agent/session within one second
  are suppressed. No conversation content is forwarded.
- A compact notification overlay with screen wake and sound, five-second expiry,
  and dismissal that preserves the original button action.
- A manually started focus/rest cycle, four weather pages, light/dark/scheduled
  themes, and up to 20 selected radio stations with desktop search and ordering.
- Phone setup for up to five remembered Wi-Fi networks; automatic reconnect,
  LAN discovery on an available client port, and device/desktop settings sync.
- A menu-bar macOS app with one universal Apple Silicon/Intel DMG or ZIP, and
  a verified merged firmware image for the ESP32-C3's 8 MB Flash.

## Install

Use the firmware and app from the same release. See the attached `INSTALL.txt`
or `INSTALL.zh_CN.txt` for Mac installation, BlackHole 2ch, input-method setup,
permissions and pairing. The source tree's `docs/vibe/README.md` covers device
flashing and controls. The merged `FoloToy-AI-Passport-full.bin` is written at
`0x0`; a full install can reset device settings. Check `SHA256SUMS.txt`.

Packages use free ad-hoc signing and are not Apple notarized. A first launch or
update can require per-app approval and refreshed keyboard permission. No paid
Apple account is required and the app does not use Keychain.

## Validation limits

Build and host tests are separate from physical-device acceptance. Physical
Intel and clean-machine installation still need verification. The current
client is macOS-only and supports one paired computer. Agent coverage depends
on version and local entry point; radio availability depends on the stream,
network, format, and available device resources. Third-party notices accompany
both packages. Final release-specific results must be reviewed before publishing.
