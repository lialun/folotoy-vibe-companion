**English** · [简体中文](README.zh_CN.md)

# Set up and use Vibe Companion

For the project overview, see the [root README](../../README.en.md). This guide
covers the device; [Mac installation](install.md) covers the companion and
[Agent integration](agents.md) covers hooks and plugins.

See [Device connections](connections.md) for LAN/BLE pairing and switching computers.

See [Agent quotas](quotas.md) for automatic quota cards and [Custom Agents](agents-custom.md) for local notification and quota callbacks.

## First setup

### Install firmware

The hardware is FoloToy AI Passport: ESP32-C3, 8 MB Flash, 240 × 320 color screen. This image
is not intended for arbitrary ESP32 boards.

**Recommended: install online.** Connect the powered device to your computer with
a USB data cable. In a browser supporting Web Serial, open the
[Vibe Companion play page](https://ai-passport.folotoy.cn/en/plays/2174/) and follow
its instructions to install the firmware directly. Download the matching desktop
app from [GitHub Releases](https://github.com/lialun/folotoy-vibe-companion/releases/latest).

**Manual local installation:** alternatively, download the firmware and app from
the **same GitHub Release**. Open the [official FoloToy flasher](https://ai-passport.folotoy.cn/tools/web-flasher/)
and choose **local firmware**. Select the downloaded
`FoloToy-AI-Passport-full.bin`; the merged image is written from **`0x0`**.
A full installation replaces the old firmware and can reset saved settings.
Do not write an application-only binary at `0x0`.

Developers preserving existing settings should use the compatible application
partition workflow in [firmware layout](../development/engineering/firmware-layout.md).
The release image is a complete install, not a promise of data-preserving updates.

### Choose a connection

A new device shows a connection welcome page with Bluetooth, Wi-Fi and Later
on its physical key rail. BLE works without device networking. In Vibe Companion,
choose the matching device suffix, click Connect and confirm on the device.
BLE advertising is already active on the welcome page; the computer can request
a connection before a transport guide is selected. Later is remembered.
Reopen the guide from **Functions → Device settings → Connect computer**.
Previously paired devices go straight Home and reconnect after an update.

### Add Wi-Fi (optional)

Choose **Wi-Fi** on the welcome page or **Functions → Device settings → Wi-Fi**.
Join the open `Vibe-Setup-...` hotspot shown on the device with
your phone and wait for its setup page to open. If it has not appeared after
10 seconds, open `http://192.168.4.1/` in a browser. Select a **2.4 GHz** network,
enter its password and connect.
After successful connection and saving, the hotspot closes and the device
continues waiting for computer approval during first use; authorization is required
before returning Home. A browser tab may retain a
success message if the phone does not allow automatic closing.

The device remembers up to five networks. Boot and reconnect try the last
successful network and then other saved networks automatically. To add a network,
open **Functions → Device settings → Wi-Fi** on the device. The phone page
lets you connect to saved networks without retyping passwords, update passwords,
or forget a network. Forgetting the active network removes its saved record but
does not terminate that connection immediately. No other account/profile setup
is required.
An unavailable saved network never opens the hotspot or forces Wi-Fi setup.

### Pair the computer

Install the app, BlackHole 2ch and a compatible input method using the
[Mac guide](install.md). Wi-Fi requires a reachable LAN;
BLE does not require the device to join a network. See [Device connections](connections.md).

Click the device’s **Connect** button and accept with its upper key. In the input method,
select **BlackHole 2ch** and a hold-to-talk shortcut matching the companion.
Grant keyboard-control permission to the **app itself**. Permission previously
given to Terminal is separate. The current version pairs one device with one
computer. Pairing uses a private local file, not Keychain.

Focus an input field, hold UP, speak, and release. Tap DOWN to insert Enter.
The computer must remain awake, including when its lid is closed.

## The three buttons

From top to bottom: **UP, DOWN, SETTINGS**. DOWN is physically in the middle;
SETTINGS is at the bottom. The firmware BSP calls SETTINGS `OK`.

| Page | UP | DOWN | SETTINGS |
| --- | --- | --- | --- |
| Home / weather | Hold: voice; release: finish | Tap: Enter; hold: open Pomodoro ready | Tap: next weather page; hold: functions |
| Pomodoro | Hold: voice | Tap: Enter; hold: exit | Tap: start/pause/resume; hold: functions |
| Radio | Hold: voice; release: reconnect listening | Tap: Enter; hold: exit confirmation | Tap: next selected station; hold: functions |
| Exit confirmation | Tap: cancel | Tap: confirm exit | No action |
| Functions / settings | Previous/increase | Next/decrease | Tap: enter/edit/confirm; hold: back |

One hold/release is one action; releasing a hold does not add an Enter or confirm
an exit. The rail on the right shows the meaning in the current context.

On a sleeping Home screen, ready voice input (UP) and a ready short Enter
(DOWN) execute while waking. Every other gesture only wakes, including its
hold and release; press again to operate the timer, radio or page. Connection
requests wake the screen automatically. Confirm/cancel accepts a new gesture
only after the request is rendered and all previous keys have been released.

## Pomodoro

Hold DOWN from weather to open the ready phase. Tap SETTINGS to start, pause,
or resume. Voice and Enter remain available. On completion, the device sounds
its alarm and shows completion for ten seconds, then waits at the next
focus/rest phase. **Every phase starts manually** with SETTINGS.

Hold DOWN to leave. A phase that has not started exits directly; a running or
paused phase opens confirmation. UP cancels, DOWN ends it. A running countdown
continues in the confirmation and settings pages; cancel preserves its state.
Changing configured durations affects the next phase, not the active deadline.

## Weather

Weather is always enabled. Choose its city in the app; the initial city is
Beijing. Tap SETTINGS to read four cached pages. Pages do not turn automatically
and the current reading page is retained across screen sleep and wake.

| Page | Contents |
| --- | --- |
| Current | Temperature, condition, high/low, apparent temperature, humidity, maximum UV and level |
| Next six hours | Aligned hourly temperature curve, rain probability and precipitation |
| Air and wind | US AQI, PM2.5 concentration, wind, gusts and daily maximum wind |
| Three days | Daily condition icons, high/low and rain information |

Data comes from [Open-Meteo](https://open-meteo.com/). The connected desktop app
normally refreshes it every 15 minutes while audio is idle, over Wi-Fi or BLE.
The device retains direct Wi-Fi downloading as a fallback when memory permits. Missing samples use a dash; stale
weather retains its age, and air older than 30 minutes is hidden. Wind is shown
on the Beaufort scale. **US AQI is not China AQI**; PM2.5 is in µg/m³. UV shows a
number and a level rather than color alone. Turning a page makes no network
request or Flash write.

## Radio

Open **Functions → Radio**. In the desktop app, use **Recommended**, **Search**,
or **Custom** to build My stations, then drag to order and save. Up to 20
stations are supported. Search can be empty; the Chinese checkbox is optional.
Custom addresses must be supported public audio/playlist URLs, not web pages.
The app probes for recognizable media before adding a custom station; a Mac
probe passing does not guarantee continuous delivery by that broadcaster.

Keep the desktop app running and the computer online. The Mac fetches and decodes
MP3/AAC/HLS; the device plays the resulting audio over Wi-Fi or BLE. Device Wi-Fi
is optional. Both links can stay connected; only one carries radio at a time,
with acknowledgements and fallback. The speaker receives 16 kHz mono audio.

SETTINGS advances to the next station. Hold UP for voice: playback resources
are released, voice gets priority, and the previous listening intent reconnects
after release. This returns to live audio, not the previously buffered moment.
There is no separate pause entry. Hold DOWN for exit confirmation; listening
continues until DOWN confirms. UP cancels without interrupting it.

Change volume in device settings or save a **volume-only** change from the app
while listening. Other settings changes require audio to stop. Stream formats,
server availability, network jitter and device memory can affect playback.

## Settings and themes

The menu-bar app configures brightness, volume, 3–60-second screen timeout,
microphone gain, timer durations, weather city, radio stations and themes.
Hover the microphone-gain help icon for adjustment guidance. Close the window
to keep the app running in the menu bar; quit from that menu to stop it.

Device settings are authoritative. The app reads them when connecting and
receives later changes. Unsaved local edits remain drafts; Save writes only
changed fields, then adopts the device's acknowledgement. Reload discards a
draft. A firmware update does not replace saved values with new defaults.

New-device defaults: brightness 50%, volume 80%, screen timeout 15 seconds,
25-minute focus/5-minute rest, and automatic light at 08:00/dark at 21:00.
Switch dark, light or automatic in the app or **Device settings → Screen theme**.
Editing a setting shows an outline, filled value and arrows. Theme and brightness
are independent.

Date/time and scheduled themes use the same device clock. Wi-Fi can synchronize
it without the computer; the companion also supplies time. With no valid time,
the status bar has no real date/time and automatic theme falls back to dark.
There is no battery-backed clock that keeps time through a physical power-off.
The Pomodoro uses monotonic elapsed time, not wall-clock adjustments.

Unpairing the computer changes pairing only. It preserves Wi-Fi and other
settings; pair again on the new computer. Software shutdown is not provided.

## Agent reminders and standby diagnostics

Install the integrations you need through **Agent notifications**. See
[the coverage and verification guide](agents.md). A reminder wakes the screen,
sounds, and overlays the current page for five seconds. A physical button closes
it and continues the same gesture. No session ID is displayed as a name.

For standby checks, open **Connection and voice → Standby diagnostics**. Use
Reset timing, unplug USB, let the screen sleep for 5–10 minutes, then Read stats.
Statistics include screen-off/light-sleep time and power-lock information; they
are not current measurement. USB connection itself can block automatic light
sleep. Reset does not clear boot-cumulative power-lock history. The panel also
shows dynamic-heap usage, free space, largest free block, and historical minimum
free space. **Copy report** exports reported counters for troubleshooting; there
is no continuous sampling. Battery percentage is the fuel gauge's estimate,
not a calibrated current or capacity reading.

## Troubleshooting

- **No device found:** check the same reachable LAN, local-network permission,
  VPN/firewall rules and Wi-Fi client isolation. Reopen the client or retry.
- **Voice does not start:** first test the physical shortcut in an ordinary input
  field. Check the input method, matching shortcut, BlackHole and app permission.
  Secure keyboard input held by another application can block both real and
  simulated shortcuts.
- **Permission is enabled but not ready:** return to the app for automatic
  rechecking. An ad-hoc update can invalidate old permission; the Mac guide
  describes replacing the old entry.
- **Radio fails:** try another known station and check the address/format.
  Record the station and on-screen error for a useful report.

For reproducible problems, follow [Support](../../.github/SUPPORT.md).
