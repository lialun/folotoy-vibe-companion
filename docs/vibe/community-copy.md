[简体中文](community-copy.zh_CN.md) · **English**

# Vibe Companion · Voice & AI quotas

## Overview

Voice input. AI quota management. Your own Vibe companion. A companion desktop app makes device connections, voice setup and Agent integrations easy to manage.

Wi-Fi and Bluetooth work together for steadier, more continuous voice input. Both links carry your voice while the computer merges audio and removes duplicate copies. One can fill gaps left by the other, and a usable surviving link can continue carrying speech after a disconnection. Single-link use also benefits from short audio history and missing-block repair.

Bring remaining allowances and task alerts from everyday Agents such as Codex, Claude Code and OpenCode Go to AI Passport beside you. See allowances, reset times and task status at a glance.

Express your ideas naturally with voice input. The device lights up when a task finishes, needs approval or asks a question, so you spend less time checking chat windows.

Bluetooth and Wi-Fi fit a variety of environments. Set up built-in Agent integrations with a click, or connect your own Agent to make the companion part of your workflow.

Focus with a Pomodoro timer, take a break with radio, and keep the weather close. Make it your desk companion for work and everyday life.

## Instructions

1. Flash the firmware, install and open the Vibe Companion desktop app.
2. Choose Bluetooth or Wi-Fi, click Connect in the app, and confirm on the device.
3. Enable what you need: install integrations under Agent alerts; follow the app's guide for voice input.

Your own Agent: open Agent alerts → Connect your own Agent and copy the prompt to your coding Agent.

Desktop downloads: https://github.com/lialun/folotoy-vibe-companion/releases
Keep the computer awake and the app running. The computer fetches radio and online quota data.

## Changes

v1.0.0

Added simultaneous Wi-Fi and Bluetooth voice delivery with merging and deduplication on the computer.
Single-link voice gains short audio history, missing-block repair and retries for temporary send failures. Link selection resists brief jitter, trailing speech is preserved, and queued Enter waits for voice output.
Update both firmware and desktop app and connect both links to enable dual-link voice. Sustained outages beyond the recovery window can still lose audio.
Known issue: radio can still stutter after switching delivery to BLE when retransmissions accumulate; this release does not include a radio scheduling fix.
