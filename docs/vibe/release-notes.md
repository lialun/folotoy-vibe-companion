**English** · [简体中文](release-notes.zh_CN.md)

# Vibe Companion v0.9.0

## New in this release

Agent quotas join voice input and task alerts on your desk. The companion collects
Codex, Claude Code and OpenCode Go allowances from existing Agent integrations,
including remaining percentages and reset times. Go keeps its five-hour, weekly
and monthly windows separate rather than presenting a currency balance.

Quota pages come before weather and share a consistent global page counter.
Three percentage windows fit one page. Collection normally runs every five
minutes during recent activity, stops after ten idle minutes and backs off on
failure. A sample expires after fifteen minutes thirty seconds; reconnects do
not renew it. Unavailable data does not create an error card.

The desktop app adds **Connect your own Agent**. Copy its prompt to integrate
local task alerts and real quota observations through the account-private
helper. Custom Agents keep their own display names; no conversation text or
credentials are sent to the device. See the source guides for field limits and
callback requirements.

This release also improves Bluetooth notification handling when the operating
system changes an unrelated subscription. Use the matching firmware and app.

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

Install the firmware and app from this same release. See `INSTALL.txt` or
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

Local checks cover the firmware build, host/native tests, actual-font LVGL
renders in both themes, live Codex quotas, installed custom-helper callbacks,
and physical Wi-Fi/BLE quota acceptance, retry expiry and conditional clearing.
The installed custom notification was acknowledged by the device, and its quota
sample appeared in the desktop app and expired without renewal.

Claude Code and OpenCode Go adapters were checked with interface/response
fixtures; live subscriptions for those two services were not available locally.
The new three-window device view has rendered-font coverage but no separate
physical visual confirmation. Power/battery, clean-system installation, physical
Apple Silicon/minimum macOS, and physical multiple-computer/device tests remain
outside the completed local scope. Earlier voice, Enter, radio/resume, re-pairing
and phone Wi-Fi acceptance are recorded in the previous release.

The current desktop client supports macOS. One device authorizes one computer
at a time; a confirmed handover replaces that authorization. Keep the computer
awake and the app running. The computer fetches radio and online quota data.
