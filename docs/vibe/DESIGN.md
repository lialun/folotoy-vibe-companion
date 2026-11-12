**English** · [简体中文](DESIGN.zh_CN.md)

# Vibe Companion design contract

This document defines the product's interaction, visual and engineering
conventions. Use it when adding a feature or reviewing a change. The
[implementation record](implementation.md) contains historical experiments and
exact validation results; those checkpoints do not override this contract.
[AGENTS.md](../../AGENTS.md) governs repository workflows and the BSP boundary.

## 1. Product priorities

Vibe Companion is a **voice-input companion**, with useful information and
small applications around that core. Priorities, in order:

1. Reliable, quickly accessible voice input and deliberate Enter actions.
2. Legible information and predictable three-button operation.
3. Stable operation on the ESP32-C3's internal RAM.
4. Low energy use, without trading away voice reliability or stable playback.
5. Additional applications that fit the same interaction model.

Do not add an introductory showcase or launcher before Home. Do not implement
an application on top of the upstream hardware-test UI. Reuse the official BSP
and isolated library logic, and keep the product interface in `main/vibe/`.

## 2. The device's mental model

Home is the desktop: a compact voice status area remains above one active
content module. The default module displays weather. Pomodoro and radio replace
that module; **weather, Pomodoro and radio are mutually exclusive**.

Voice is an interruptible service available from Home in all three modules.
Radio is silent during voice input. Settings, provisioning and confirmation
screens are separate pages with their own key meanings; they do not inherit
Home's voice/Enter bindings.

A timer's state is independent of page navigation. Entering settings or an exit
confirmation does not pause it. Moving from an active or paused Pomodoro to
radio requires a direct confirmation that ends the timer before starting radio.
Future modules must define their own entry, active, interruption and exit states.

## 3. Physical keys and gestures

From top to bottom the physical keys are **UP, DOWN, SETTINGS**. DOWN is physically
in the middle. The BSP calls SETTINGS `OK`; product text calls it SETTINGS.
Never refer to an unexplained middle/bottom key in user-facing instructions.

| Context | UP | DOWN | SETTINGS |
| --- | --- | --- | --- |
| Home, default module | Hold: voice; release: finish | Tap: Enter; hold: Pomodoro ready | Tap: next weather page; hold: functions |
| Pomodoro ready | Hold: voice | Tap: Enter; hold: exit immediately | Tap: start; hold: functions |
| Pomodoro running | Hold: voice | Tap: Enter; hold: exit confirmation | Tap: pause; hold: functions |
| Pomodoro paused | Hold: voice | Tap: Enter; hold: exit confirmation | Tap: resume; hold: functions |
| Next phase ready | Hold: voice | Tap: Enter; hold: exit immediately | Tap: start next phase; hold: functions |
| Radio | Hold: voice, release: resume previous listening intent | Tap: Enter; hold: exit confirmation | Tap: next selected station; hold: functions |
| Timer exit/switch confirmation | Tap: cancel | Tap: exit or switch | No action |
| Radio exit confirmation | Tap: cancel, keep listening intent | Tap: stop and exit | No action |
| Functions / settings navigation | Tap: previous item | Tap: next item | Tap: enter/edit; hold: back |
| Editing a setting | Tap: increase/previous choice | Tap: decrease/next choice | Tap: finish; hold: leave editing |

A press-hold-release is **one gesture**. A long press suppresses the short action
on release. Long DOWN must never send an additional Enter or immediately leave
a page it just opened. Two short presses remain two deliberate actions; there
is no double-click vocabulary in this version. During recording, other Home
actions are suppressed until UP is released.

Home's long DOWN is an application shortcut: enter Pomodoro from the default
module; exit the current application otherwise. Its rail icon changes as soon
as the ready screen is entered. SETTINGS supplies the active application's
primary short action. Radio dedicates short SETTINGS to next station and long
DOWN to open exit confirmation. Keep the stream and current station unchanged
while the confirmation is visible and when UP cancels. Only short DOWN confirms
the stop and exit; the entry hold's release cannot confirm. Use the same narrow
key rail and direct choices as the timer's exit page. Functions contains only device settings and radio, with
no manual pause/resume entry; automatic voice preemption and resumption remain.

## 4. Pomodoro flow

```mermaid
stateDiagram-v2
    Default --> Ready: Hold DOWN
    Ready --> Running: Tap SETTINGS
    Running --> Paused: Tap SETTINGS
    Paused --> Running: Tap SETTINGS
    Running --> NextReady: Deadline and alarm
    NextReady --> Running: Tap SETTINGS
    Ready --> Default: Hold DOWN
    NextReady --> Default: Hold DOWN
```

- Entering Ready does not start time. The next short SETTINGS starts the phase.
- Show remaining time prominently, identify focus/rest and display its duration.
  Desktop editing must show focus and rest durations with equal clarity.
- Use monotonic deadlines. Screen sleep, voice, navigation and theme changes do
  not alter the countdown. Changing preferences does not reset the active phase.
- On completion, play the existing device alarm and show completion feedback
  for ten seconds. Then retain the next focus/rest phase ready for one-key
  start. It never starts automatically and does not leave Pomodoro by itself.
- Opening Exit preserves both the countdown and running/paused state. UP cancels
  without changing either; DOWN ends the timer. These are direct actions,
  not a two-item menu requiring selection and confirmation.
- Ready and NextReady can exit immediately because no countdown is active.
- Copy is short: a state, a time and an actionable choice. Avoid explanatory
  slogans and descriptions of which default content appears after exit.

## 5. Information and feedback

Voice states distinguish network unavailable, unpaired, computer unavailable,
prerequisites missing, ready, recording and retryable audio failure. An error
must not permanently disable Enter or prevent a later capture attempt. A timer
continues without a computer. Failed/disconnected voice input never becomes a local
recording waiting to be uploaded later.

Weather has four pages: current conditions; six upcoming full hours; air and
wind; three-day forecast. Short SETTINGS advances manually; time passing never
changes the page. Keep that page for reading until the next screen wake.
Switching pages uses cached data and never causes a
network request or Flash write.

Hourly temperature, rain probability and precipitation share aligned time
columns. Do not connect missing temperature samples. Three-day rows keep text
on the left and larger weather artwork on the right. Icons cover known weather
codes, day/night variants and an unknown fallback. Wind is shown as a Beaufort
level. Label the source's **US AQI** explicitly, and PM2.5 in micrograms/m³;
missing/stale values use a dash, not zero. Maximum UV includes a textual level.
Detailed data and freshness rules are in the [usage guide](README.md).

Radio shows the station, loading/playing/stopped/error state and list position.
Centre the current name in the existing 20 px font and show the next station in
16 px, with bounded long names; list positions are not fictitious MHz values.
Draw tuning ticks directly. Only the name band slides for 200 ms on a station
change; playback itself is static, and voice/status/key rails stay fixed.
Rapid changes replace the active animation rather than queueing. Voice, sleep,
navigation and palette replacement cancel it before teardown. Reuse labels and
fonts without a raster canvas, transparent layer or task.
A Mac probe checks a custom source before adding it. My stations shows names
and ordering controls without successful-playback badges; a probe never promises
future availability. Empty lists and unsupported streams have explicit states.

## 6. Device visual system

### Canvas and information hierarchy

The screen is **240 × 320 pixels**, physically small and strongly rounded by
the enclosure. Design at native resolution and inspect on the real screen.
Large desktop previews do not establish readability or edge safety.

- Use a continuous canvas. Do not put the voice and content areas into two
  ornamental cards, and do not reserve a tall application-title area.
- The top status region is approximately 24 px high. Its 16 px text and icons
  have explicit top/side insets for the enclosure. Display Wi-Fi bars, date/time
  and a numeric battery percentage immediately to the left of the battery icon.
  Keep the icon anchored as digits change, reserve space for `100%`, and show
  `--%` if the fuel gauge is unavailable. Reuse the existing battery label and
  sampling on boot and screen wake, then at one-minute intervals while visible.
  Skip fuel-gauge reads while blanked; do not add a polling task or timer.
- Voice is a compact status row. Its approximately 28 px waveform lives to the
  left; ready state uses one vertically centred line, exceptional/recording
  states can use two lines. Never leave an empty subtitle line in the layout.
- Give the remaining area to weather, a readable countdown or radio information.
- Keep the right key rail about 16 px wide. Current Home labels begin at
  `x=221`; their positions follow physical keys, not equal thirds of the display.
  New geometry needs real-device review, including rounded-corner clipping.

### Typography and icons

Chinese body text uses the maintained **20 px** font; secondary text and key
labels use **16 px**. Use larger 36/48 px digits for primary temperature/time
where space permits. Avoid shrinking body text to fit decorative content.
Tighten spacing deliberately; current CJK labels use -1 px letter spacing, and
rail labels use their own compact vertical spacing.

Keep the compact waveform proportionate to text. Use a timer symbol for the
Pomodoro shortcut and an exit symbol inside an application. Desktop branding
uses the companion/orbit mark, not a microphone-shaped application icon.

Before adding Chinese text, validate font coverage, selected widget fonts,
symbol fallbacks and dynamic names. Never hide missing-glyph boxes. Check both
short and long labels, missing values, three-digit numbers and station names.

### Color and themes

Use semantic palette tokens rather than new per-widget colors:

| Role | Dark | Light |
| --- | --- | --- |
| Canvas | `#14232E` | `#C6C1AD` |
| Primary text | `#F0EBDD` | `#1B2529` |
| Secondary text | `#A8BBC2` | `#3F4B50` |
| Voice | `#51DFC4` | `#005749` |
| Focus | `#FFB47D` | `#8C2C0C` |
| Rest / Enter | `#8CC9FA` | `#004E96` |
| Weather emphasis | `#F2CB73` | `#654600` |

In Light, use neutral primary and secondary text rather than assigning a similar
dark hue to every value. Current temperature and ordinary settings values use
primary ink; voice, focus/rest, UV and AQI keep their semantic colours alongside
explicit labels. Keep selected settings text neutral and show editing with the
existing reversed value, arrows and outline. Font size and position carry the
hierarchy even when the LCD makes hues look similar. After RGB565 quantisation,
the text inks target at least 4.5:1 contrast on their actual backgrounds; this is
a rendered-colour check, not a measurement of the physical LCD.

Use more chromatic vector marks:
teal #00765D, blue #0965AD, orange #B54716 and ochre #936100. Apply these to the
existing voice/weather icons, key marks, tuning needle, progress and chart
strokes. Do not brighten small text to match an icon or add image surfaces.

Light means a muted warm canvas, not bright white panels. Color reinforces text
and symbols; it cannot be the only indication of state or availability. Device
LCD appearance can differ from rendered RGB previews, so validate both themes
at normal brightness on hardware.

Support dark, light and scheduled themes. Defaults are 08:00 light / 21:00 dark;
the device executes the schedule, including intervals crossing midnight.
Before valid time is available, scheduled mode uses dark. Theme does not change
brightness, timer state or playback. Evaluate the current device time on boot,
first valid clock and wake; do not wait for a boundary or persist its last colour.
If a cold boot has no valid clock, acquire UTC over SNTP after Wi-Fi obtains an
address. Client time sync remains available; use the persisted timezone until
the client updates it. Manual dark and light modes do not execute the schedule.

The device system clock is the single calendar-time authority; SNTP
and the authenticated client set that clock, rather than supplying separate UI
clocks. Use an already valid local clock immediately. After calibration,
re-evaluate calendar-dependent displays and themes without restarting the
device, applications or Pomodoro. Durations use the device's monotonic clock so
wall-clock corrections cannot change elapsed focus time.

Network time acquisition belongs to the existing network task and SDK TCP/IP
service. Only start while time is invalid, outside capture/provisioning, with
at least 20 KiB free heap. Each attempt has a 12-second deadline and one server;
rotate among Aliyun, Cloudflare and pool.ntp.org on failure, with 5-second,
1-minute, 5-minute and capped 15-minute retry delays. An active attempt briefly
checks completion at most once per second while the screen is off. A valid
clock, loss of Wi-Fi or capture/setup stops and releases the SNTP service.
After success no periodic SNTP request, timer or completion polling remains.
Do not add a dedicated task, semaphore, screen wake or Flash timestamp writes.
Without internet or a client, keep the clock unavailable and scheduled mode
dark until a valid source succeeds; do not invent time from a stored timestamp.

During audio use, a validated volume-only configuration update is permitted;
other changed fields remain gated. Persist and acknowledge through the existing
revision path. The playback owner applies the new volume at the next PCM frame;
do not restart the stream or touch the muted capture output from the config path.

### Consistent key rail and editing

Use the right rail on every page, including settings and confirmations. A dot
means a short press; an elongated horizontal mark means hold. Keep the gesture
mark and short label together. Do not put another full set of key instructions
at the bottom of the page. Disabled actions are visually subdued.

Editing must change the selected value itself: visible row outline, contrasting
value background and directional arrows. A changed rail alone is insufficient.
Use concise status labels and useful errors. Avoid duplicate instructions,
marketing copy and decorative readiness dots.

## 7. Desktop interaction

Use computer/client for connection, pairing and device actions. Name an operating
system only for a platform-specific feature or requirement; this wording does
not imply support for an unimplemented platform. The current client is macOS-only.

The macOS app is primarily a menu-bar utility. Opening its window can show the
Dock icon; closing the red window button hides that icon and keeps the receiver
running. Only Quit stops the application. Sidebar rows have full-row hit areas.
Use the companion mark consistently in the app, Dock and menu bar.

Settings group related controls and show meaningful units, ranges and save
results. Put longer explanations in contextual help, including what microphone
gain changes and when to increase/decrease it. The input-method hint states
hold-to-talk compatibility and shortcut conflict risk. Do not repeat device key
tutorials below connection or timer settings, or explain window closing in a
permanent footer. Show focus and rest durations together.

Radio uses **library + selected stations**: recommendations, name search and
custom URL detection feed an ordered device list of up to 20 entries. Search
shows the name field, Chinese checkbox and Search button on one row. The fifteen
curated recommendations also form the initial station list, in the same order;
preserve any saved list, including an empty list, across firmware updates. Search
defaults to Chinese-language stations; unchecking Chinese removes the language
filter. Blank names are allowed. Requests use ten results per page, hide broken
entries and sort by votes descending, with no hidden country or bitrate limit. Users can reorder/remove entries and save an empty list. Keep
custom input on detection failure. The client manages the list, not device playback.

The macOS UI, keyboard sender and networking workers share one main-thread
PostEvent preflight snapshot. A worker must not synchronously wait on the main
thread for a permission query while shutdown waits for that worker.
After a keyboard-permission grant, recheck when the app becomes active. If the
running process retains a denied state, reload once after returning from the
explicitly requested system authorization, or when a fresh same-executable probe
confirms a stale result. Child-process probes can also inherit a cached denial.
Wait for active operations to finish; the new engine must pass the real PostEvent
check before showing ready. Preserve unsaved settings, station ordering, form inputs and navigation
in a private, short-lived, one-use recovery file. Close the old UDP listener
before relaunch and allow at most one reload to avoid a loop. Never substitute
an unrelated permission check or send test keystrokes to infer permission.

## 8. Configuration and connectivity

Phone hotspot setup configures **Wi-Fi only**, using the password-free
`Vibe-Setup-...` AP and captive portal. Successful connection and storage close
the AP automatically without another confirmation. Routine settings use the
normal LAN and never require returning to hotspot mode.

Keep up to five Wi-Fi profiles, managed only in the phone portal. The portal
separates saved and nearby networks, reuses saved passwords without returning
them to the browser, supports password replacement and forgetting. Same-name
updates replace credentials only after a successful connection and save.
Full capacity requires explicitly forgetting an old network; never silently
replace one. Forget removes persistence without interrupting the current link.

On every boot/outage, try the last successful profile and then every other
saved profile, including hidden/unavailable-in-scan SSIDs. Driver failure moves
to the next profile; a fifteen-second attempt timeout bounds missing callbacks.
Failed rounds back off 5/10/20/40/60 seconds. Healthy links trigger no background
scans or profile hopping. A startup setup hotspot is offered only after all
saved profiles have failed a round and twenty seconds have elapsed; a later
outage keeps Home and its timer. Once the setup hotspot is open, stop an
unfinished automatic connection attempt and pause background profile retries.
The phone must be able to scan without the driver simultaneously connecting
to an unavailable saved network. Test only the network explicitly submitted
in the portal; resume automatic retry when setup is closed. An already healthy
STA link may remain connected while the phone manages profiles.
Late failures for a previous SSID, or while the driver already has a newer
association, must not clear readiness or advance that connection's retry.
Select the next profile after applying timeouts, not before. Automatic setup
must never interrupt an attempt or association still waiting for DHCP.

Scan errors, driver-busy state and allocation failure must be returned as
errors, not successful empty arrays. Keep the saved list usable if scanning
fails; distinguish an actual empty 2.4 GHz scan and offer refresh/manual entry.
Drain scan records individually, retain at most sixteen results, and release
the driver's unread list on success, truncation and every post-scan failure.
Do not add a resident scan task, cache or record-array allocation.

The credential bank is 487 bytes; its versioned compact NVS value is at most
491 bytes. Reuse the old Wi-Fi key: decode/migrate the original single-network
value and replace it only after a successful write. Do not retain a second copy
that could resurrect forgotten credentials. Old single-network firmware cannot
read the new multi-profile format after downgrade; provisioning is then needed.
No new resident worker or task stack is allocated. Protect the bank separately
from controller state; network/HTTP workers own persistence, never button callbacks.

Only the device discovery port is fixed (UDP 33333). The client binds port zero
and lets the OS assign an exclusive available port. Discovery, settings and audio
reuse that socket. An authenticated beacon selects its actual source IP and UDP
port; the advertised JSON port cannot redirect replies. Ordinary packets must
match both endpoint fields. A client restart or device address/boot change clears
stale requests, event deduplication and held shortcuts, then reloads device data.
Unconfirmed writes are not replayed across a reconnect; local drafts stay local.
New firmware accepts old fixed-port clients. Install the matching firmware before
this dynamic-port client; the old firmware cannot route its replies correctly.

Device values are authoritative. The client loads them on connection/reconnection;
revision notifications and existing keepalives repair missed updates. Unsaved
Client edits are drafts: unmodified fields follow the device, modified fields stay
local. Save submits only modified fields; only a successful device reply means
persisted. Storage failure must not report success. Avoid redundant NVS writes.
Station lists use a separate revisioned, authenticated atomic transaction;
compact v2 storage reads old v1 blobs. Old 16-station firmware cannot read a
newly saved v2 list after downgrade; export/record stations before downgrading.
The 24 KiB NVS is shared with Wi-Fi/pairing/preferences: twenty is the entry
ceiling, not an unlimited address-storage guarantee. If storage is exhausted,
saving fails without replacing the previous list;
partial transfers never replace the saved list.

Defaults: brightness 50%, volume 80%, screen timeout 15 s (range 3–60 s), focus
25 min, rest 5 min, scheduled theme 08:00/21:00 and Beijing weather. Weather is
always available as the default module. Existing saved values take precedence
on updates. Local settings contain basic hardware controls, theme, Wi-Fi setup
and unpairing; there is no software power-off action.

The six-digit code bootstraps pairing with a random token. The token is stored
in device NVS and an account-local client file (directory 0700, file 0600), with no
Keychain calls. LAN transport is not encrypted; this is a trusted-LAN design.
Audio is routed live, not saved by this project. macOS Accessibility is a separate
OS permission for synthetic keyboard events. Shortcuts are ordinary key events
and can conflict with other software or be blocked by secure input.

One device remembers multiple computers, with exactly one active owner. Wi-Fi
and BLE authenticate the same persistent computer identity; discovery cannot
take over a healthy connection. The user can explicitly switch to a paired
computer. Wireless toggles control availability; routing is automatic. See the
[connection contract](connections.md) for leases, reconnection, memory and power
rules. Remote radio playback control remains outside the product contract.
Agent notifications follow the integration contract below.

## 9. Resource ownership and power

Use a single audio owner for microphone, codec, chime and radio; button callbacks
only enqueue events. Preserve voice service state and its **3,200-byte PCM buffer
from startup**, including while radio plays. Keeping RAM reserved does not keep
the microphone powered. Keep the codec asleep when audio is idle.

Weather updates use a disposable worker. HTTP/TLS, response bodies and parsing
resources are released, then another task deletes the finished worker and
reclaims its **8 KiB stack**. Audio cancels and joins that worker before using its
resources. Sleeping or suspending a task alone does not reclaim its stack.

Radio owns only its disposable compressed FIFO. Voice ends the session and
releases the FIFO before capture; listening restarts only when still requested.
The computer owns source connections, decoding and its larger bounded cache.

Every allocation needs one owner, a lifetime and a failure policy:

| Resource | Owner / lifetime | Release and failure rule |
| --- | --- | --- |
| Voice PCM, 3,200 B | Audio service / application lifetime | Reserve at startup; roll back failed startup; never lend it to another module |
| Core task stacks, 33 KiB total | Main, audio, network, LVGL / application lifetime | Measure worst paths before resizing; do not add a permanent task per page |
| LVGL pool 26 KiB + display DMA 9,600 B | UI / application lifetime | Rebuild module objects inside the pool; keep DMA requirements distinct from ordinary heap |
| Weather stack 8 KiB and HTTP/TLS/body/parse workspace | Disposable weather worker / one update | Cancel, clean up, externally join/delete before audio; allocation failure defers weather |
| Radio compressed FIFO, 15,704 / 23,556 B | Audio owner / one session | Detach callbacks before freeing; release on voice/exit/change/failure; retain 16 KiB headroom |
| Wi-Fi credential bank, 487 B | Network service / application lifetime | Five profiles; separate mutex; serialize at most 491 payload bytes on existing worker stacks, save before replacing; never return passwords through LAN/portal reads |
| Saved station list | Catalog / application lifetime | Allocate `12 + 306 × count` bytes (maximum 6,132), not a permanent 20-slot array; store only actual null-terminated names/URLs in NVS; accept legacy 16-slot blobs |
| Station-list transfer staging | Configuration transaction / bounded transfer | Allocate by incoming count only while audio is idle; cancel before audio starts; promote the staged list on atomic commit without another full-size copy; preserve the old list on failure; expire abandoned transfers |

For each new feature, document its **simultaneous peak**, including library
allocations, task stacks, DMA and temporary copies. Calculate overlap from actual
lifecycles, not by summing mutually exclusive states or ignoring background jobs.
Every partial initialisation path must release what it owns. Do not free another
owner's live buffers or delete a task still using them. Record free heap,
largest usable block, minimum free heap and stack high-water marks for repeated
entry/exit and voice interruption. A successful single allocation does not
establish an adequate memory budget. Resource-policy changes require lifecycle,
allocation-failure and repeated-cleanup tests plus a device regression check.

Radio is relayed by the active computer over its selected Wi-Fi or BLE route.
The computer fetches and decodes MP3/AAC/HLS, resamples to 24 kHz mono and sends
independent 50 ms IMA ADPCM blocks. The device needs no radio HTTP/HLS/TLS,
AAC/MP3 decoder, reader task or separate PCM workspace. It reuses the resident
3,200-byte voice PCM block while not recording. The computer must remain awake,
online and running the companion; device Wi-Fi may be off for BLE listening.

The device owns two or three disposable 7,852-byte segments (26 or 39 frames,
15,704 or 23,556 bytes, approximately 1.3 or 1.95 seconds). Initialize the audio
format before admission, retain 16 KiB of control/network heap, and reject
startup if two segments cannot fit. No permanent queue/task is added per station.
Cumulative credits bound the sender to available receiver capacity; duplicates,
gaps, wrong routes and expired streams cannot grow the FIFO. Each session gets
a new random stream number, bound to the current owner route and lease. Credits
repeat every 200 ms while listening; the desktop retries the first missing block
without enlarging the window. There is no audio polling or relay process at idle.

The computer keeps at most 200 encoded blocks (about 118 KiB / 10 seconds),
using producer backpressure. It bundles a hash-pinned, minimal LGPL FFmpeg
subprocess, its corresponding source archive, license and exact build recipe;
no Homebrew installation or runtime binary download. HTTP/HTTPS are the only
station inputs. Stop the process, queued downlink packets and receiver session
on voice, exit, station/owner/route change or failure. Voice resumes live radio
through a new stream after release. Enforce bounded connection/read deadlines
and process teardown; never retain radio buffers merely to accelerate resume.

Preserve required interrupt code, stack guards, display/audio DMA and modem
sleep. Weather retains authenticated HTTPS and dynamic TLS buffers. Smaller
firmware and fewer device allocations do not establish battery-current savings;
measure both real transports, voice preemption and prolonged listening.

New paired clients negotiate a 10-second combined readiness/time beacon and
30-second peer expiry. The installed firmware accepts 5–30 seconds; its hello
advertises the upper bound of that capability, not the chosen interval. This
client policy change needs no firmware update or additional device RAM.
Discovery remains every two seconds; peers without negotiation retain 5/15
seconds. Client 0.8.0 still chooses 30/90 seconds on the same firmware.
Audio/keyboard readiness changes send immediately.
Voice, keys, configuration and Agent notifications are event-driven and never
wait for a heartbeat. Replies replace device fallback pings, due only after the
negotiated interval plus two seconds without outgoing traffic. Passive expiry
allows three missed beacons; this interval prioritizes connection-state updates
without changing event delivery. Client
exit marks audio/keyboard unavailable immediately. Idle Wi-Fi remains MIN_MODEM;
changing listen intervals requires separate latency/reception measurements.

Standby must leave a real automatic-Light-sleep window. Three ADC keys share
one low-level GPIO interrupt: once all gestures settle, the button component
stops its 20 ms polling timer. Restore the pin's digital input only for idle
wake, without adding internal pulls to the external resistor ladder. An ISR
resumes the component; its task disables wake before sampling ADC again. Keep
the first gesture, release and long-press semantics. Arming failure resumes
polling, and ISR setup failure retains normal polling. Teardown disables wake
and removes the handler before releasing ADC. Validate all three actual keys.

Blank controller/network waits are up to 30 seconds, interrupted immediately
by queue/socket events. The controller caps its wait at a running timer's exact
deadline. Provisioning, reconnect, recording and visible UI keep short waits;
network commands and display transitions wake the socket. Receive timestamps
are taken after blocking, so fresh packets cannot look thirty seconds old.
Cached RSSI is queried while visible. Memory logs run only with a USB host.
Weather still refreshes every fifteen minutes, with at most one idle wait of
scheduling delay and no additional resident worker. Radio/voice ownership and
Wi-Fi recovery policy remain unchanged.

LVGL reads the ESP monotonic clock on demand, with a 30-second port maintenance
tick and idle task wait. Its 20 ms visible refresh is independent of that tick.
Blank pauses the refresh timer and sends LCD Sleep In; wake restores the panel
before refresh/backlight. Do not globally stop LVGL: pinned LVGL 9.5 / port 2.9.0
would busy-poll. Keep the same screens, pool, DMA buffer and timer deadlines.
Queue voice/alert work before the panel's roughly 100 ms command delay.
Hold a no-Light-sleep lock while the display is awake: the board's default LEDC
mode does not sustain backlight PWM in Light-sleep. Release it only after blank
and panel sleep succeed; reacquire before restoring the panel/backlight.

An enumerated USB host temporarily prevents automatic Light-sleep so C3 serial
monitoring and flashing remain usable; unplug USB for battery-only acceptance.
CW2017 exposes SOC and cell voltage, not measured current. SOC/time trends can
compare discharge sessions but must not be presented as instantaneous mA/mW.
On-demand authenticated power.get/power.reset returns successful/rejected sleep
counts, successful-call time (including entry/exit overhead), blank-only time,
longest call, USB/key-wake status, fresh SOC/mV and bounded lock history. No new
sampling timer/task/history ring or Flash writes. The public PM exit callback
uses the pinned SDK sleep context to exclude rejected/skipped calls. IDF PM
profiling adds per-lock counters; the SDK-private context must be revalidated
when upgrading IDF. The desktop Connection page keeps a single Device status panel expanded, with
runtime statistics above saved events and one combined Copy report action. Read
and Reset stay manual; never poll statistics automatically. Saved events remain
available when disconnected. Connection indicators separately show Wi-Fi and
Bluetooth reachability and the active route; voice permission/driver failures
belong in the voice section and never change the successful connection title.
Separate window totals, instantaneous readings and boot-cumulative counters.
Group identically named lock types, preserve each duration without summing
overlapping intervals, and retain raw lock records in the copied report.
Keep all reported diagnostic fields visible, including total successful sleep
time, both connected/disconnected USB states, and lock-report completeness.
The copied report targets model-assisted analysis: use original milliseconds
and bytes, raw lock records and explicit unavailable values; omit explanatory
paragraphs and derived values a reader can calculate. Counter scope, heap scope
and sleep-overhead definitions stay in code and this document; the report is
intended to accompany the matching source. Omit transport request IDs.
The two I2S locks belong to the separate capture and playback channels.
Display short sleep durations in milliseconds rather than rounding to tenths
of a second. Reading wakes CPU/Wi-Fi and current lock activity
may include the read itself. Reset clears session totals, not boot lock history.
These are sleep/lock diagnostics, not measured electrical draw.
The current C3 build adds 176 B BSS and 904 B IRAM versus the preceding battery
display/manual-weather build. PM profiling grows each allocated lock from
32 B to 56 B; GPIO ISR registration and the single PM exit callback also need
small dynamic structures. The compact lock dump is built only on request,
using a bounded stack workspace and temporary JSON/stdio allocations. Include
these costs in heap measurements; do not describe diagnostics as free.
Each explicit read also queries the existing allocator counters for the
8-bit-capable heap, before allocating the diagnostic response: allocated
bytes, available bytes, largest contiguous free block, and historical minimum.
These are runtime heap statistics, not total chip SRAM usage; firmware static
sections are excluded. The historical minimum sums per-region low watermarks
that may have occurred at different times. Reset does not clear it.
Use the largest free block when investigating failed decoder/cache allocations;
total available bytes alone do not establish whether a large allocation fits.
This adds no resident sampling buffer, task, timer or Flash writes. The request
uses one temporary `multi_heap_info_t` (28 bytes on ESP32-C3) and temporary JSON
allocations, released after transmission. Old firmware without heap fields
shows an unavailable state rather than fabricated zeros.
Against the preceding local release candidate, this addition leaves BSS,
initialized DRAM and IRAM unchanged; the merged firmware grows by 176 bytes.
Measure battery-side current separately; a USB charger meter includes charging
and conversion effects. No battery-life claim follows from a successful build.

Use notification/queue waits and cached data. Do not search/probe station
libraries on the ESP32. Keep radio modem sleep enabled unless measurements
justify a bounded alternative. Voice may temporarily use low-latency Wi-Fi.
Stop retrying after bounded repeated failures. Stable radio reception takes
priority over an assumed power saving; increased Wi-Fi power alone does not fix
an upstream outage. A finite FIFO cannot conceal arbitrary network gaps.

Current cancellation is cooperative. Body reads check at short timeouts; DNS,
connection establishment, TLS and weather requests can take longer. Radio
resume includes a fresh connection and prefill. Do not claim instant voice or
instant radio recovery without device timing. Current/energy improvements need
instrument measurements, not inference from lower task counts.

## 10. Change and acceptance rules

### Bounded diagnostic contract

Device diagnostics use eight 24-byte typed entries plus two counters: a fixed
200-byte ring and small synchronization/announcement state. No new task, timer,
Flash writes, message-string cache, or continuous heap sampling is permitted.
Capture heap size/largest block only when recording an event. Reuse the network
worker to announce new data and the authenticated current-owner control channel
to fetch batches of at most three. Existing status replies advertise the newest
sequence so dropped announcements are repaired without a separate heartbeat.
Voice defers desktop catch-up requests. A reconnect fetches the surviving window;
boot identity and sequence deduplicate retries. Explicitly mark offline overflow.
The RAM window does not promise persistence across device reset or power loss.

The desktop owns durable history: at most 500 validated records and a 512 KiB file
limit, asynchronous serial file I/O, atomic replacement, account-local access.
Preserve unreadable files and report storage failure rather than silently clearing
evidence. Keep code, numeric detail, stream, transport, receipt time, uptime,
heap counters and firmware/ELF identity. Do not persist arbitrary event bodies,
URLs, credentials, addresses, transcripts, user paths or raw decoder stderr.
Decoder stderr is drained into a temporary 4 KiB buffer only while playing and
classified into fixed error codes and an optional HTTP status. Known preset names
may be shown; custom names are omitted. The report uses the same typed whitelist.
Normal heartbeats, audio blocks and successful keys are not diagnostic records.

Radio errors on both sides share a stream number. A source error and its resulting
device stop remain separate evidence; UI should not disguise them as independent
station failures or discard one. Reset reasons establish a restart, not its full
call stack. Radio diagnostics must not be presented as proof of network, memory
or power causes without matching measurements.

A substantial new interaction or visual direction is first shown as native-size
static states and discussed before implementation. Preserve approved controls
and defaults. Evaluate happy paths, cancellation, missing data, disconnection,
heap pressure, sleep/wake and repeated switching, not just a screenshot.

Keep state machines and protocols host-testable. All non-LVGL-thread UI access
uses the BSP lock. Reuse the BSP for hardware and keep feature policy in the
application. Do not change pin assignments or replace the accepted UI with a
demo shell while fixing another feature.

Run the full repository gate and retain the exact merged-image/ELF identity.
Record Build, Host tests, Device tests and Unverified separately. Real tests
include voice during radio, repeated resume, timer exit/cancel state, both LCD
themes, long station/city labels and theme/screen-sleep boundaries. Follow the
owner's current flashing authorization and verify data-preserving updates.
Neither a build nor a flash proves audio stability, legibility or battery life.

## Agent notification contract

Agent adapters live on the computer. Reuse the paired transport and audio owner; use fixed bounded device state, expiry and deduplication. Never forward transcripts or interfere with approval decisions. Keep voice and physical-key semantics intact. See [integration and memory contract](agents.md).

Install only attention-seeking callbacks. The first event is immediate; suppress additional callbacks for the same Agent and known session for one second, across event kinds, without extending the window. Other sessions and Agents stay independent. Do not group missing identities or add lifecycle hooks, delayed candidates or metadata lookups. Keep exact-event retry filtering and bounded desktop bookkeeping. Generic idle and per-tool failures are not new human-action requests.

Display a compact non-modal overlay for five seconds. Preserve uncovered page
content and keep its updates running. Any physical press dismisses it and
continues that same gesture. Do not add a dedicated dismiss action, backdrop,
page snapshot, new task or larger LVGL pool. Reuse the existing fonts and draw
after page content; keep voice available and measure drawing peaks and repeated
show/hide allocation before delivery.
