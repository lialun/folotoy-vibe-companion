**English** · [简体中文](implementation.zh_CN.md)

# Implementation and validation record

Historical checkpoints below retain the decisions and tests from each iteration.
For the current product contract, read [DESIGN](DESIGN.md).

## Baseline and reuse

The feature branch is `feature/vibe-companion`, based on official main commit
`488beda41309f052d81f08d0016d65a3fd85eb7e`. Official display, battery, button,
I2C, codec recovery and codec suspend implementations are retained. The only
BSP extension is a bounded microphone-gain setter with sleep/wake tests.
ESP-IDF is 5.5.3; LVGL remains pinned to the official lock's 9.5.0 rather than
upgrading as a side effect of adding the audio decoder dependency.

References, under the repositories' MIT license:

- `passport-os` commit `b70879b3c4be0751cb728b8b0c421dd2d1524fdb`:
  independent-block IMA ADPCM, chime samples/algorithm, CJK bitmap asset, bounded
  HLS/HTTP source reader and ICY stripping. The full launcher, old BSP and
  mutually exclusive voice/Pomodoro applications are not imported.
- `folo-ai-passport-voice` commit `d89e0ca506f48a9e2a7c4ec9ac7d57c773450c84`:
  UDP framing, BlackHole audio route, shortcut lifecycle and left/right
  modifier flag behavior. The Mac implementation is ported to native Swift.
  The previously working local WeType side-mask fix is preserved.

Source repositories: [official](https://github.com/FoloToy/ai-passport),
[passport-os](https://github.com/manchunx7-bit/passport-os),
[voice](https://github.com/manchunx7-bit/folo-ai-passport-voice).
Retain the repository [MIT license](../../LICENSE). The community font is a
Source Han Sans SC 16px, 2bpp uncompressed subset with 4,717 glyphs, generated
with `lv_font_conv` from community symbol inventories. Its
[OFL license](../../assets/fonts/vibe-font-license.txt) accompanies redistribution.
Static application strings are checked against glyph comments; dynamic city /
station names can still contain unsupported glyphs and must be inspected on
device. Montserrat is used explicitly for symbols/status text.

## Ownership and protocol

`main/vibe/model.c` is an ESP-independent state machine. A controller consumes
bounded button/network queues; callbacks only enqueue. Short presses are
derived from release unless the same gesture produced a long press, so a long
DOWN never inserts Enter and two fast short presses remain two actions.
All LVGL work uses the official lock and the screen persists for application
lifetime. There is no background task referencing a deleted page.

One audio task serializes microphone, radio, chime and codec operations. The
official codec sleep path stops I2S and suspends ES8311 when idle. A radio
session closes its HTTP and decoder resources before the task changes owner.
The timer uses monotonic deadlines independently of page, network and wall time.
Timers persist across page switches, not power loss; preferences persist in NVS.

UDP port 33333 retains types 1 audio, 2 JSON event, 3 PC beacon and 4 ping.
Audio packets are 807 bytes: type, sequence, `0x80`, 804-byte independent IMA
block, 16 kHz mono / 100 ms. Loss is bounded per block. Critical events repeat
three times with an event ID; the Mac deduplicates them, including Enter.
Config requests have IDs, validation, explicit replies and bounded retries.
Repeated equal settings do not rewrite NVS. Settings writes report storage errors.

Pairing uses a six-digit on-device code and a random per-Mac token. Subsequent
control/ready messages require the token; it is stored in an account-local Mac configuration file and
device NVS. The current app does not call Keychain. JSON size and depth are bounded. This is a trusted-LAN protocol,
not encrypted transport. No microphone recording, network password or token is
written into source or application logs. Wi-Fi configuration HTTP exists only
during hotspot setup, accepts requests on the AP interface, and exposes Wi-Fi
fields only. Credentials are stored only after a successful connection.
The revised setup hotspot is password-free. Captive DNS is bound to the AP
address and polled by the existing network worker only during setup, with a
DHCP portal hint and HTTP redirects for phone detection probes. HTTP interface
checks accept IPv4 and IPv4-mapped IPv6 sockets; the former IPv4-only check
incorrectly rejected the dual-stack HTTP server's requests. Host tests cover
both address families, bounded DNS/EDNS replies and malformed queries.
Phone forms use a same-origin custom submission header, explicit connection /
failure states and a manual setup-URL fallback. Successful connection and NVS
commit arm a three-second grace period, then the device closes its own hotspot
and returns Home without another phone request. Failure stays on the same form.
The page attempts to close itself; ordinary browser tabs may retain the success
message. Captive popup behavior was confirmed on the user's phone.
Automatic hotspot setup is offered once during an unconnected startup. A later
Wi-Fi outage stays on Home and retries without replacing the page; manual
network setup remains available in device settings.

Mac speech keys are released on end, disconnect, input timeout, sleep and Quit.
An end-of-speech drain grace preserves trailing audio. The app does not capture
or globally replace the computer's microphone. WeType selects BlackHole explicitly.

## Power policy

No BLE, idle capture, continuous level meter, animation or cloud connection.
Idle Wi-Fi uses minimum modem sleep; ESP-IDF automatic light sleep/tickless idle
is requested. Speech temporarily disables modem sleep for lower latency and
returns to modem sleep on release. Discovery is every two seconds until found;
paired operation uses five-second unicast beacons and pings. Weather refreshes
every fifteen minutes, backs off after failure and starts only while audio is
idle. Pairing, settings and button actions are infrequent traffic.

ADC button polling and display infrastructure still wake the CPU periodically;
USB debugging can affect sleep. These choices are not a battery-life claim.
Measure battery-only current with the display off, display on, voice and radio.
No software power-off entry is provided. Agent
notifications and low-power notification wake design are deferred.

## Initial build and device checkpoint

The development checkpoint has passed focused state/timer tests, bounded JSON
and ADPCM tests, the existing upstream static gate, native Swift protocol and
shortcut tests, and C/Swift sample-for-sample decoding of a shared waveform.
The local BlackHole output queue opened and ran successfully. Both architecture
tests ran locally (arm64 native and x86_64 under Rosetta); physical Intel
hardware still needs acceptance. The free release route builds ad-hoc signed
universal DMG/ZIP packages, installation guides and full payload checksums.

The local preview passed the complete isolated gate (`./tools/validate.sh`),
archive verification, universal Mac build, ad-hoc signature verification,
DMG checksum verification, packaged arm64/Rosetta self-tests and a byte-for-byte
DMG/ZIP app comparison. Release staging generated and verified checksums for
all six payload files. Build: PASS. Host tests: PASS.

Firmware SHA-256:
`5976913757018f5d67e33653590faff228df75af3b336a984997d100ea1bb94e`.
Matching ELF SHA-256:
`bdbab0d5c0c1bfc4c6adcf446baf765db64586096267b78225ac298687ce7bee`.
Its verified debug archive is `build/firmware/<firmware-sha256>/`.
The merged image is 2,606,672 bytes, written from `0x0`; its embedded local
application version is `488beda-dirty` because this development is uncommitted.
Release asset identities are also included in the delivered `SHA256SUMS.txt`.

After explicit user approval, this exact image was flashed from `0x0` to a
confirmed ESP32-C3 with 8 MB Flash. The write hash passed, and a twenty-second
startup observation showed successful LVGL/Wi-Fi/codec initialization, codec
low-power readback and `ESP_OK` power management, without a panic or reboot
loop in that window. No whole-chip erase was performed. The Mac app was
installed in Applications and took over UDP 33333 after the old receiver
stopped normally. Device tests are partial: flash/boot passed; interaction
acceptance is in progress with the user.

Device rendering, Wi-Fi setup, radio continuity, actual
WeType shortcut permission, physical Intel hardware, clean-machine installation,
downloaded-app Gatekeeper/Keychain behavior, real current and end-to-end acceptance remain device /
user checks until explicitly tested. The GitHub workflow is prepared, not run
or published from this local branch.

## Provisioning repair checkpoint

The user reported that the first phone page only displayed `Use device Wi-Fi`,
and requested a password-free hotspot, clearer setup UI and automatic phone
portal opening. The dual-stack socket/address mismatch was identified and
corrected; initial phone provisioning is recorded as failed, not accepted.

The repaired image passed the complete isolated gate and archive verification.
Its SHA-256 is
`458e9ab844ffc5d7961deefc5bcdb3ce72c653f1ef5714298e2c83f5427194dc`;
the matching ELF is
`a6b911c5caa7e8d37f3f765ce3aff780ecf7581d02c0f146d17834c44621e747`.
The merged image is 2,617,280 bytes. All 163 static non-ASCII characters are
covered. A 390 by 844 browser preview with a mock server passed network-list
deduplication, selecting/submitting a network, completion, and manual entry when
scan results are empty. The native Mac application remains compatible.

Build: PASS. Host tests: PASS. On 2026-11-08 the user approved this exact
repair image. It was flashed from `0x0` to the same confirmed ESP32-C3 / 8 MB
device; the write hash passed. Twenty seconds of startup logs showed successful
initialization and codec low-power readback without a panic or portal startup
error. The monitor was closed before asking the user to test the phone flow.
The user confirmed automatic phone popup, successful Wi-Fi configuration and a
pairing code on Home. Native Mac pairing succeeded. The user requested removal
of the extra Finish step. Persistence across reboot, layout and audio acceptance
remain pending; these are not implied by successful provisioning.
This is not a published release.

## Automatic setup completion revision

Removed the extra success page and Finish request. Connection and a successful
NVS commit now close setup after a three-second status grace period. A connection
loss during that grace period cancels closing and allows retry. No Internet
probe or permanent extra timer/task is added. The host timing test covers the
boundary and disconnected cases. Each accepted Mac discovery beacon now receives
a hello: the previous shared ping cadence could prevent discovery after the
Mac process restarted at the same IP.

Build: PASS. Host tests: PASS, including 162 static non-ASCII glyphs. A 390 by
844 mock-browser test confirmed a single submit, inline success without a second
button, and an editable retry form after failure. The verified merged image is
2,616,640 bytes, SHA-256
`d71635cd563243abae3bd0d780775b983440d6f61b13c510a6bf3fd9fb55da10`;
matching ELF
`9e74ef4726374cb3f531b18af52ca3d9c299240d6eb768bb4ddb42ec291b0a63`.
Its application image is SHA-256
`421b8bcbe6c7df1cab87dca291aa168a69698373cd4fd9612e5a5c6d9f71e6cd`,
at `0x10000`. The partition table is byte-identical to the approved repair image,
both use IDF 5.5.3, and the NVS schema is unchanged. An app-only update can retain
the existing bootloader, Wi-Fi settings and pairing.

The user approved this exact application-only update on 2026-11-08. It was
written to the confirmed ESP32-C3 at `0x10000`, without a rebuild, full-chip
erase, or writes to NVS/bootloader/partition table. The write hash passed. A
bounded twenty-second startup observation matched ELF `9e74ef472...`, showed
automatic connection using saved Wi-Fi, and contained no panic/assert/watchdog
marker. The serial monitor was then closed. The Mac app retained pairing and
automatically reconnected, including after quitting and reopening the app.
Device tests: PARTIAL. Automatic exit of a new phone setup session, native
voice, background timer and battery current remain unverified.

Mac app 0.1.1 checks and requests event-posting permission through
`CGPreflightPostEventAccess` / `CGRequestPostEventAccess`. It no longer also
requires AX UI access, which is not used by the keyboard sender. Universal
arm64/x86_64 builds, native and Rosetta self-tests, ad-hoc signature verification,
and DMG integrity verification passed. The local app was updated. The user
allowed its existing pairing item in Keychain; a subsequent restart did not
prompt again. The system permission switch was already enabled, but the new
process still reported no event-posting access. After removing/re-adding the
current installed app in System Settings and restarting it, the app reported
permission granted and ready to speak. The ad-hoc designated requirement changed
between app builds, consistent with a stale grant; no TCC database was modified.
A fresh TextEdit document was prepared for user acceptance. Native voice
acceptance is pending, not implied by the protocol tests.

## Audio acceptance failure and correction

The user tested image `d71635...da10` and reported an audio-error/restart message,
no speech response, and no Enter response. Native voice acceptance is FAIL for
that image; its successful boot and pairing checks do not establish audio I/O.

The worker suspended the BSP codec after startup and after every job, but capture,
alarm and radio never called `bsp_audio_wake()` before setting a format. The BSP
correctly rejects format changes while asleep. The first capture consequently
latched an audio fault, and the controller also used that fault to disable Enter.
An application-level host regression reproduced the first-capture failure with
the original code; existing BSP recovery tests did not cover this missing call.

The correction initializes/wakes the codec before each audio operation, returns
it to sleep when the worker is idle, permits another UP press to retry a failed
operation, and separates Enter readiness from audio readiness. Regression tests
cover idle-to-capture twice, failed wake/read followed by retry, alarm/radio wake,
return to idle, and independent Enter. The complete isolated gate passed,
including all 161 static non-ASCII glyphs. The verified merged image is
2,617,664 bytes, SHA-256
`a7aa3fc16df84a0991d64966f2e6f73341568cb5796fcaafb47a60bf1a7c9c93`;
matching ELF
`378cf50945d364833e3bd9abd794ae1d09e1d1727d952bb05677a0e672e73563`.
The application-only image is SHA-256
`2d52391bd942dcc59c615c32a2dbb4de3b7438abb85e2892623132bb7b7d99cf`.

The user explicitly approved this correction on 2026-11-08. Its application was
written at `0x10000` on the same confirmed ESP32-C3 / 8 MB; hash verification
passed. The partition table matches the previously flashed image byte for byte,
and the NVS schema is unchanged. No rebuild, full erase, or NVS/bootloader/table
write was performed. Startup matched ELF `378cf5094...`; Wi-Fi and Mac pairing
were retained. The user confirmed that voice input and Enter now work, and the
TextEdit document visibly received recognized text and newlines.

The bounded ninety-second serial observation recorded eight successful codec
wakes and nine successful suspend readbacks, with no capture/preparation failure,
panic, assertion or watchdog marker. IDF printed repeated-disable diagnostics
for already-stopped I2S channels; BSP treats `ESP_ERR_INVALID_STATE` as already
stopped, and the observed captures completed. The monitor is closed.

Build: PASS. Host tests: PASS. Device tests: PASS for the observed flashing,
startup, retained connection, repeated speech input and Enter. The user also
confirmed the one-minute countdown on Home, concurrent speech, completion chime
and ten-second completion message followed by the normal lower area. Radio,
screen-wake speech and battery current remain unverified. Focus was temporarily changed from 25 to 1 minute for this
test, then restored to 25 minutes, saved and read back; break remains 5 minutes.
The user's weather selection was preserved during the restore. The user then
confirmed a new phone-setup session: the open hotspot/captive form accepted one
Connect submission, automatically closed setup and returned the device Home,
without another Finish button. Pairing was preserved and the Mac app remained
connected and ready after the test.

Keychain alternatives are being discussed; storage and pairing remain unchanged.
The user asked about future multi-Mac switching and explicitly requested no
development of that feature now. The currently implemented single-Mac limit
must not be described as already supporting switching.


## Desktop feedback and interaction design checkpoint (2026-11-08)

The Mac preview now identifies all three physical keys (UP at the top, DOWN in
 the middle, SETTINGS at the bottom), scopes these instructions to Home, expands
sidebar hit regions to the whole row, removes the window-close footer copy, and
explains input-method requirements and microphone gain in help text. The tested
left Control + left Option + V shortcut is retained. These are ordinary posted
keyboard events, not reserved conflict-free events; the UI advises against single
keys, single modifiers and common editing shortcuts without claiming to enumerate
other applications' registrations.

The duration summary gives focus and break equal visual weight. The user clarified
that rotation means automatically showing the next phase in a waiting state,
then starting it with one short SETTINGS press on Home. It does not mean automatic
phase timing. UP speech and DOWN Enter stay available; long DOWN opens timer
management, and long SETTINGS opens functions. Completion copy clears after ten
seconds while the waiting next phase stays; explicitly ending the session restores
weather. This device behavior is a design proposal, not yet implemented.

The Mac timeout control and save validation now use 3–60 seconds. Legacy 61–300
second replies remain decodable and are normalized into a visible 60-second draft,
with an explanatory message. The installed firmware still requires at least ten
seconds, and the preview explicitly marks 3–9 seconds as requiring a future
firmware update. The device-side bounds remain unchanged while Home design is
under review.

PM2.5 was omitted during the weather expansion. It is restored to the design,
using the original separate Open-Meteo air-quality request with independent
validity and units; data integration is pending. Weather icons will cover every
code in the provider's WMO table, including freezing precipitation and hail, with
text for intensity, day/night variants and an unknown-data fallback. Existing
static pictures are examples, not completed icon integration.

The user accepted the Mac radio-library design: all original curated presets,
searchable regional stations and custom URLs feed an independent ordered My
Stations list; region is a filter rather than an automatic replacement policy.
Selection, persistence and protocol changes are pending. An optional background
radio design keeps Home speech/Enter intact: choose a station in radio controls,
return Home to listen, suspend playback for speech or chimes and resume afterward.
It requires bounded audio ownership and real interruption/rebuffering tests;
background listening is not implemented yet.

Mac universal build, strict ad-hoc signature verification, native protocol/codec/
shortcut/configuration tests and DMG verification passed. Native preview checks
confirmed empty-space sidebar clicks, the 3-second control value and complete
three-key/help text. The repository host suite also passed. Initial IDF activation
selected system Python 3.9 and failed; activating the existing project toolchain
with Python 3.12 then passed the isolated firmware stage. Its merged identity is
`b443cb59013ac2d670905760ff57adbc0520bcc34a290cec53fa20c38d11086e`
and matching ELF is
`f2d51efd25d4bf82896c9ff02e0097c6ae448fde777bc35abd9bb696b3978bce`.
This validates the previous working-tree firmware, not the new design proposals.
No firmware source was edited in this feedback step, no image was flashed, no
production Mac app was replaced, and no release was published. Device acceptance
for the new interface and pending behaviors is NOT RUN.

## Accepted Home and phase handoff implementation (2026-11-08)

This checkpoint supersedes the pending status in the earlier design checkpoint.
The user authorized the compact continuous Home, manual next-phase handoff,
weather/PM2.5 and desktop feedback changes. The user then deferred radio entirely
for this iteration: device and Mac radio entry points are hidden, directory I/O
is dormant, and no ordered-list protocol or background playback is delivered.
Future radio should replace the lower Home module and coexist with speech,
mutually exclusive with weather/Pomodoro; a full-screen-only design was rejected.

Home uses a 16 px status band, compact speech state, narrow vertical physical-key
hints and an unboxed main area. Local 48 px Lucide A8 masks cover all 29 weather
codes in the provider mapping, day/night variants and unknown fallback; descriptions
retain intensity and freezing details. Three pages rotate every six seconds.
PM2.5 has its own validated concentration/unit sample and timestamp; stale air
shows -- after thirty minutes. Both HTTPS requests are bounded to 4 KB and one
client, retain retry backoff, and cancel when audio becomes active. TLS is released
before parsing. The host transport test covers cancellation, deadline, overflow,
HTTP errors, partial/empty bodies and cleanup under ASan/UBSan.

Completed phases immediately select the next phase but never start it. Ten-second
completion copy becomes a waiting heading; short Home SETTINGS starts the next
phase, including from screen sleep. Long SETTINGS still opens functions. End
session is confirmed in timer controls and restores weather. The active phase
snapshots its duration, so Mac edits do not move the deadline/progress. Regression
tests cover focus/break handoff, waiting indefinitely, concurrent speech, click
suppression, short-timeout completion visibility and ending the session.

Timeout bounds are now 3–60 seconds on both ends; preference blob layout, Wi-Fi
and pairing are unchanged. Old 61–300 second preferences clamp on load. The Mac
recognizes the optional new minimum capability while still decoding old replies.
The app retains ordinary configurable keyboard events, Keychain pairing, free
ad-hoc signing, accessory activation and the custom non-microphone companion mark.

Actual pinned LVGL rendering uses the production UI and both CJK fonts. The new
48 px digits require a larger temporary glyph surface; the pool increases from
24 to 28 KB. Host fixtures measured a peak of 19,600 bytes in the allocator's
usable pool, and unchanged refreshes cause no display flush. The icon masks use
27,648 Flash bytes. No per-frame waveform animation, additional persistent
connection or radio search runs in this iteration. Actual current, peak internal
RAM during TLS/audio overlap and physical key alignment still need board tests.

Native desktop preview checks confirmed whole-row empty-space sidebar selection,
3-second timeout, both durations, all physical-key descriptions and gain help.
Preview mode does not access pairing, LAN, login items or device configuration.
The production Mac app and connected firmware remain unchanged until update
approval. The exact final firmware/ELF identity and gate results belong in the
matching delivered validation record, not the earlier preview hashes above.

### Installed review build and standing update authorization

Final complete gate: PASS. Host tests: PASS, including 316 static non-ASCII
glyphs in both linked font sizes. Native universal Mac build, self-tests, strict
signature verification and DMG verification: PASS. The verified merged image is
2,925,072 bytes, SHA-256
`921032129567d36c718c6b09b242674f0517b8b90c6e4139c108d53dc609d1df`;
matching ELF
`dafd9b5a3820edb4adb03b8e077867b6807470a9c6ef3434c6daeb93ce3fefd9`;
application SHA-256
`1cd0041979f2ccfb34a9ddf16456b836444fabfc4e6157c0be895f556703ed7e`.
The archive is `build/firmware/<merged-sha256>/` and was reverified before use.

On 2026-11-08 the device owner explicitly changed the earlier per-build approval
rule: new firmware may be flashed directly without repeatedly asking. This is
standing authorization for routine updates to this device. Preserve Wi-Fi,
pairing and preferences using verified compatible application-only writes.
Erasing stored data or changing partition layout is not part of this normal
update path. Future work should retain this authorization and not ask again
merely because another ordinary build is ready.

The exact application above was written to the confirmed device at `0x10000`;
esptool verified its hash. The partition table is byte-identical to the last
accepted image. No NVS, bootloader or table was written. A bounded forty-second
serial window included boot with ELF prefix `dafd9b5a3...`, `app_main`, and no
panic, stack-canary, watchdog, heap-corruption or audio-preparation failure.
The monitor is closed. Wi-Fi and pairing were retained and the app reconnected.

Mac 0.2.0 replaced the fully quit previous app in Applications, with a local
backup retained. The old Accessibility entry stayed checked but did not grant
the new ad-hoc binary. The owner completed system authentication; removing the
old entry, adding the installed app again and restarting restored recognition.
A stale process/window query immediately after Quit produced a launch error;
subsequent normal Finder launch succeeded. The installed code signature is
valid; the app now reports connected, BlackHole ready and shortcut permission
ready. All owned UI preview processes were stopped; only the production receiver
is running. Keychain/pairing storage and the local shortcut remain unchanged.

The owner subsequently confirmed speech, transcription, release-to-end and
Enter work on this exact installed build. Physical UI acceptance failed:
supporting text and the status bar were too small on the approximately 5 cm
diagonal 240 x 320 screen. Stage-handoff and live PM2.5 acceptance for this build
are still pending; older device results must not be substituted. Clean
installation, physical Intel execution, sleep/wake and
measured battery current remain unverified. No Git write or release publication
was performed.

### Readability revision

Primary CJK text increases from 16 to 20 px, supporting text from 12 to 16 px,
and status text/icons from 10 to 16 px. The status band remains at the top in
24 px. The 48 px temperature/countdown is retained. Both CJK assets share the
4,717-character inventory in `assets/fonts/vibe-symbols.txt`, with 2 bpp
uncompressed bitmaps. The new `main/vibe/font_20.c` was generated from pinned
LVGL's `SourceHanSansSC-Normal.otf` with lv_font_conv 1.5.3, no prefilter or
kerning; the generated header records the conversion arguments. The obsolete
12 px asset is removed from both firmware and host preview builds.

CJK letter spacing is -1 px. Weather details use full-width rows for humidity,
wind and PM2.5 with concentration units. Forecast spacing and secondary copy
are shortened to avoid wrapping. Settings use larger row text and two-line
footers; short confirmation wording fits beside its action icon. Navigation,
audio, networking, timer state and preference layout are unchanged. Radio
remains deferred and inaccessible from navigation.

The owner requested the upper key hint higher and the bottom key hint much
lower. The narrow 16 px rail now places the upper label around y=48, retains
the middle around y=153, and lowers the settings label around y=290. In the
waiting state, the long-press function marker sits above the short-press start
label. Physical alignment remains subject to the owner's check.

The actual pinned LVGL renderer checks 22 fixtures, including negative
temperatures, severe-weather descriptions, three-digit wind speed, large PM2.5,
pairing and confirmation states. Visible labels may not wrap beyond their
explicit line count or leave the screen; unchanged refreshes may not flush.
The pool remains 28 KB; rendering peak is recorded with the final test artifact.
The Mac app binary is unchanged to preserve its working system authorization.

Readability final complete gate: PASS. Static glyph coverage: 311 characters
in both fonts. All 22 actual LVGL fixtures passed; peak 19,752 bytes in the same
28 KB pool. Merged image is 3,215,184 bytes, SHA-256
`ab7c487371d843e5ca02f1339d92dd7aa92d28547526e560738911e8e7153831`;
matching ELF
`72d7491956790e72f206cbb65b53ca6839ea97f7c4f20125cc4cc43a10624635`;
application SHA-256
`06326b321328eee117a5ed1de9e4dd8595b10ca4c32a091aef10131e6b619c81`.
The verified archive is `build/firmware/<merged-sha256>/`.

Under the standing authorization, this application was written to the same
confirmed device at 0x10000 and esptool verified its hash. Partition bytes match
the previously installed 921032 image; no NVS, partition or bootloader write.
Forty seconds of serial observation matched ELF prefix 72d749195 and app_main,
with no crash, stack, heap, watchdog or audio-preparation fault markers. The
monitor is closed and the original production Mac receiver remains running.
Physical readability/key alignment, live weather and new-image speech/button
acceptance remain unverified; successful write and boot are not those tests.

### Display edge clearances

The owner requested a 1 px downward shift for the complete status bar, 5 px
inward on each side, and a 1 px left shift for all key-rail hints. Wi-Fi begins
at x=13, the clock at x=35/y=1 and the battery at x=211/y=1. The rail shares
x=221 with its gesture markers and waiting-state function hint. Existing
vertical key alignment, typography and main content stay unchanged. All 22
actual LVGL fixtures passed; peak was 20,160 bytes in the unchanged 28 KB pool.
Pixel comparisons of five states confirm the main area below the status bar
and left of the rail is unchanged. Physical bezel/rounded-corner clearance
still requires inspection on the device.

Edge-clearance complete gate and host tests: PASS. Merged SHA-256 `7a3118493730eda391d0496282b408761bb5d4564c60dcdc9490efe032478aa0`,
matching ELF `18ca70a44089cf64aa07d64433f1fe91a71a55d0e9c974eba62d3097130abc61`, application SHA-256 `7c5afe74a2eb29f2163a93f15999756f6eb12faceceb4d4d1bf5c43b945240f8`.
The verified archive is `build/firmware/7a3118493730eda391d0496282b408761bb5d4564c60dcdc9490efe032478aa0/`.
The application was written at 0x10000 with verified hash and unchanged partition
bytes relative to ab7c487. No NVS, bootloader or table write. A forty-second
serial window matched ELF 18ca70a44 and app_main, without crash, stack, heap,
watchdog or audio-preparation fault markers. Port closed; physical clearance
acceptance remains pending. No Mac update, Git write or publication.

### Balanced voice header and larger corner clearance

A device photo showed unused space below the single-line voice state and
continued clipping near the rounded top-right corner. The voice header now
uses an original static 44 x 44 A8 waveform on the left and text from x=60 on
the right. A single state is vertically centred; states with guidance use a
20 px title above 16 px supporting text, all beside the glyph. Recording time
and the six-digit pairing code remain on the title row. The ready dot is hidden
for two-line states. There is no text below the glyph and no animation.

Relative to the previous installed edge-clearance image, the status moves
down another 5 px: clock x=40/y=6, battery x=199/y=6 and Wi-Fi starts at x=18.
The battery moves inward another 12 px. Header content and the physical key
hints retain their existing outer heights/vertical positions; five pixel
comparisons confirm the main area below y=84 is unchanged.

All 27 actual LVGL fixtures passed, including one/two-line transitions,
recording during a timer, reconnect, pairing and audio retry. Text checks now
also enforce horizontal screen bounds. Rendering peak remains 20,160 bytes
with a 28 KB pool; unchanged states do not flush. Physical rounded-corner
clearance and optical centring remain subject to on-device acceptance.

Voice-header final complete gate: PASS. Merged SHA-256 `ee5d0c93080cc3d4bbe0e55c2cb1e4082267b6f62d5509cffc3066af61bdf10a`,
matching ELF `858a22a3f555c39c0cf122c81d8b51eb531ed7c03a16f162d51f5f700a6ef700`, application SHA-256 `284731a6d54e4b26c168aa086bfa0393f01cc2874847879436227d93686f962f`.
The verified application was written at 0x10000 with unchanged partition bytes
relative to 7a311849. Hash verification and forty-second matching boot checks
passed, without crash/stack/heap/watchdog/audio-preparation fault markers.
NVS, bootloader and table were not written; monitor closed. Physical layout
acceptance is pending. Subsequent theme scheduling, unified secondary-page
key rail and simplified Pomodoro controls are design work, not in this image.

### Next design proposal — not implemented

The owner requested light/dark themes with a Mac-configured schedule, fewer
Pomodoro actions and a persistent contextual key rail on secondary pages.
Proposal-only sources and renders are isolated in `.work/theme-proposal`;
production firmware remains the ee5d0c93 voice-header image. Both palettes were
rendered with the pinned LVGL/fonts, including menu text constrained by the rail;
these previews do not implement the proposed state transitions or scheduling.

Proposed appearance controls: Light, Dark and Scheduled, separate editable
light/dark start times (initial example 07:00/19:00). Preserve current Dark by
default. Store and execute the schedule on the device using local time; use Dark
until clock synchronization, support overnight intervals and reject equal start
times. Theme and backlight brightness remain separate; no continuous animation
or additional network polling. Use stable teal voice, warm focus and blue rest
accents with text labels, and a soft light canvas.

Pending interaction confirmation: short Home SETTINGS starts focus when idle,
pauses running time, resumes paused time, or starts the waiting next phase. It
also wakes and performs that action in one gesture. Long SETTINGS opens functions
without firing a short action. UP speech and short DOWN Enter retain their core
meanings. Long DOWN for an active session opens an explicit end confirmation,
defaulting to Continue; selecting End and confirming once restores weather.
Phase handoff remains manual, with existing chime and ten-second completion copy.

Secondary pages keep the same narrow physical anchors: previous/next/confirm,
or increase/decrease/done while editing; long SETTINGS goes back. Remove footer
button instructions. Unavailable actions must be dimmed or marked unavailable,
including during speech and on the phone provisioning page; do not invent new
provisioning key actions. No new key behavior or theme was flashed. The user
previously required discussing interaction changes before implementation.

### Accepted themes and direct timer controls — implemented 2026-11-08

This increment supersedes the preceding proposal. It implements manual dark/light
and device-local schedules with overnight handling, distinct-time validation and
a dark fallback until the clock is synchronized. Appearance persists with base
preferences in a single versioned NVS `prefs3` blob, with legacy `prefs` loading
and no Wi-Fi/token layout change. Timezone is saved only when the Mac offset
changes, not at each heartbeat. No animation, new radio stack or extra RSSI
polling was introduced. Dark remains the initial default.

Short Home SETTINGS starts/pauses/resumes, including from screen sleep; long
SETTINGS opens functions without the short action. Every stage starts manually.
Long DOWN in an active timer opens Cancel/End with Cancel selected; selecting End
needs one confirmation. Secondary pages retain the same physical rail anchors,
with long marks expanded from 5 to 10 pixels and footer instructions removed.
Provisioning retains its existing long-SETTINGS return action; no new setup-key
action was added. Voice uses a 28px waveform, the label Voice, and an original
16px stopwatch entry icon rather than a loop symbol. Fonts and safe status-bar
insets are retained. Focus/rest/voice use orange/blue/teal in both palettes.

RSSI no longer treats unsampled zero as a full signal. The existing five-second
sampling uses a half-step EMA and 3dB hysteresis; disconnect clears immediately.
Sampling has its own timestamp, independent of discovery/keepalive traffic.
The Mac no longer sends discovery in reply to every device hello, eliminating
the mutual discovery reply loop. The observed first post-update RSSI was -51dBm,
three bars; this is a sample, not a measured RF or battery-life guarantee.

At the owner's explicit request, all runtime Keychain read/write calls were
removed. Random tokens use SystemRandomNumberGenerator; the file is
`~/Library/Application Support/io.github.vibecompanion.mac/pairing.json`, directory
0700/file 0600, atomic writes. Same-account processes may read it. The existing
pairing was migrated locally from the physically connected device without a
Keychain read; the temporary NVS extract was removed and never packaged. Other
Keychain-only installations without a migrated file require one re-pair.
A malformed file is rejected on load and a new pairing can repair it.

Build: PASS, complete isolated gate, merged SHA-256 `75bf0b117339c87de592331cf5746fde6ef0e2450b6b28a902b9cbf5cc53e236`,
matching ELF `6d9d9a3c15d1ab8594e97fb1173c9dc67f5f68133da873bafa83d75a1836e1e7`, application `9dad0a8beb610158c2f42a6fcf1ad7de16397735bb67189801e310f80e12d9c7`.
Host tests: PASS, one-key controls/wake/long suppression, manual phase handoff,
chime deferral, single end confirmation, theme boundaries/timezone/overnight,
legacy/versioned persistence and RSSI stability. Native Mac universal arm64/x86_64
build/self-tests, local-store permissions/corruption handling, C/Swift vectors,
strict ad-hoc signature and DMG checks pass. Binary has no SecItem/Keychain calls.
Actual LVGL rendering covers both themes, secondary pages and schedule boundaries,
including 100 consecutive theme changes; peak LVGL allocation is 19,864 bytes,
and unchanged frames do not flush.

Device tests: application-only 0x10000 flash verified; partition bytes match ee5d0c93.
NVS/Wi-Fi/pairing retained. Forty-second startup matches printed ELF prefix
6d9d9a3c1 and app_main without crash/heap/stack/watchdog fault markers; serial closed.
An additional reboot also starts cleanly. Mac 0.3.0 auto-connects using its local
file without Keychain prompts. Manual Light and Scheduled saves/readbacks succeed;
time editing was exercised. Prior brightness 61%, volume 80%, gain 30dB and
60-second timeout are retained. The owner restarted the app after refreshing its
existing PostEvent grant; both the owner and final app state confirm permission
ready and connected. Current application installed in Applications.

Unverified: new physical one-key Pomodoro and audio acceptance, physical color/
rounded-corner readability, wall-clock scheduled transition with Mac closed,
sleep/wake and clean/Intel-Mac installation, and measured power consumption.
Earlier speech/chime acceptance applies to earlier images, not this increment.
Radio remains deferred. No commit, push or public release was performed.

## 2026-11-08 — Deliberate Pomodoro entry

Weather Home ignores short SETTINGS. Long DOWN switches the lower Home module to
Pomodoro preparation, with a stationary focus duration and explicit start hint.
Short SETTINGS then starts focus, pauses it or resumes it. UP voice and short DOWN
Enter remain available throughout Home. Long DOWN within Pomodoro opens Exit/End
with Cancel selected; select Exit/End and confirm once to restore weather.
Every next phase still waits for manual start, with the existing completion chime.
A long DOWN may wake and prepare the timer in one hold; a short DOWN only wakes.

Build: **PASS** — complete isolated ESP-IDF 5.5.3 gate and verified merged/debug
archive. Mac help text source parses; the installed 0.3.0 app is unchanged and
does not need reinstalling. Its old help text does not yet describe preparation.

Host tests: **PASS** — inactive weather taps, explicit preparation without elapsed
time, voice/Enter during preparation, sleep/wake and hold suppression, start/pause/
resume, duration snapshots, manual phase handoff, deferred chimes and safe exits;
the complete repository host suite and Chinese coverage pass.

Device tests: **PASS, limited scope** — application-only update at 0x10000 after
comparing partition tables with the previously installed image; write hash verified.
NVS and stored Wi-Fi/pairing were not overwritten. Forty-second startup matched
the new ELF identity and app_main, with no crash/heap/stack/watchdog markers.
Serial closed. No physical interaction or end-to-end speech acceptance claimed.

Unverified: **owner's physical experience of the new preparation flow**, buttons,
speech/Enter, completion, actual light/dark readability, sleep current and scheduled
theme switching. Radio remains deferred. No commit, push or public release.

Actual LVGL rendering: 54 fixtures, 100 theme transitions, no label wrapping/
clipping and no unchanged-frame flushes. Peak LVGL allocation 19,864 bytes.

Merged SHA-256: `8df6b03062f4f3c0ce56318b221426e5b0cba8297b0ac1a27407b3d8dde4112d`
Matching ELF: `547bce1050f2a2897c7b5e4c3ec090e37e83c2e171c5a08b30d207eaf03a2f4a`
Application SHA-256: `eab1bed9f20e05cdf63ca53f52f61e20a44ee6553dc259bdc05aebfb4628762f`

The merged image is for **0x0** on a blank device or intentional full refresh and
may reset stored data. This preserving update used only the matching application
at **0x10000**; never write that application-only image at 0x0.

## 2026-11-08 — Direct exit, defaults and device-authoritative sync

Weather is the desktop. Long DOWN opens Pomodoro preparation, without counting;
short SETTINGS starts, pauses or resumes within Pomodoro. Long DOWN within the
app opens its exit page. There, short UP cancels, short DOWN exits to weather,
and SETTINGS is inactive. No selection cursor or additional confirmation remains.
A running timer continues on the exit page; Cancel preserves the timer state.
The entry hold cannot accidentally trigger Exit. Settings pages retain their
navigation controls. Radio is deferred; no radio behavior is delivered.

Fresh defaults: scheduled theme at 08:00 light / 21:00 dark, brightness 50%,
screen timeout 15 seconds, output volume 80%. Existing custom settings survive
updates. Weather is always enabled; empty legacy locations default to Beijing,
while selected custom cities remain. The Mac off switch has been removed.

Saved device settings are authoritative; the Mac holds an editable draft.
Successful device edits notify Mac. Versions on the existing keepalive recover
missed updates; synchronization adds no new polling task or periodic Flash write.
Unedited fields follow device updates; unsaved local edits survive. Save sends
only edited fields and uses the device reply as confirmation. If the same field
is edited on both sides, the Mac draft wins only when Save is pressed. Reload
discards the draft. Input-method shortcut and login startup remain Mac-local.

Build: **PASS** — complete isolated ESP-IDF 5.5.3 gate, merged/debug archive;
Mac universal arm64/x86_64 compilation, native self-tests, C/Swift wire vectors,
strict ad-hoc signature, ZIP/DMG packaging and DMG verification. No paid signing,
notarization or runtime Keychain calls. No Keychain symbols in the built binary.

Host tests: **PASS** — direct Cancel/Exit in prepared/running/paused/waiting states,
entry-hold suppression, screen sleep, preserved deadlines, manual phase handoff,
voice/Enter behavior, defaults, legacy weather normalization preserving custom
settings, theme boundaries and preference migration. Mac tests cover remote edits
while a draft exists, same-field conflicts, edited-field patches, acknowledged
saves and edits during an in-flight save. Complete repository host gate passes.

Device tests: **PASS, limited scope** — partition table matches the preceding
8df6b030 image; only application at 0x10000 written and hash verified. Wi-Fi,
pairing and settings retained. Forty-second startup matches ELF prefix and
app_main, with no crash/heap/stack/watchdog markers; serial closed. Installed
Mac 0.3.1 reconnects, BlackHole and shortcut permission are ready after refreshing
the old app grant; no Keychain prompt. Mac UI confirmed Beijing without an off
switch and read back scheduled 08:00/21:00, brightness 50%, timeout 15s, volume 80%.
A separate authenticated brightness-only API update 50->49->50 appeared in the
running Mac automatically; a temporary unsaved volume draft 79% survived the
second update, then Reload restored 80%. Device volume was not changed by that
draft. This exercises revision recovery and draft merge, not physical button edits.

Unverified: **physical Cancel/Exit and speech acceptance for this image**, physical
button notification path, actual theme transitions with Mac closed, rounded-corner
readability, clean Mac/physical Intel execution and measured battery current.
Earlier owner acceptance does not prove this image's new behavior. No commit,
push or public GitHub Release.

Actual LVGL rendering: 56 scenes and 100 live theme changes pass; labels do
not gain unwanted wrapping or go outside the LCD, and unchanged frames do not
flush. Peak LVGL allocation 19,864 bytes. Exit pages use fewer objects than menus.

Merged SHA-256: `776fe74c17cf3eaf8f54fd6a7492fae1921c6ec1acd1e1d260cb9693e968410a`
Matching ELF: `9d24f60d64cd496cfd72cebf30b0eb12cfca7811e84519ca4d22288c93acdfbe`
Application SHA-256: `e62b3f593d3ba72043dca320d0bf55bc927b2538fce8b14e9b94751f3c477549`

The merged image is for **0x0** on a blank device or deliberate full refresh and
may reset stored data. This preservation update used its matching application
at **0x10000**, not the merged file. Do not write an app-only image at 0x0.

## 2026-11-08 — Concise UI and physical-pause diagnostics (Mac 0.3.2)

Removed duplicate Home/button instructions, decorative page subtitles and the voice-ready dot.
Device settings now end at Mac unpairing; no software shutdown action remains.
Exit shows only its actual phase/state and remaining time. Mac keeps necessary values,
errors, all three physical-key descriptions and detailed gain/compatibility help.
Reload clears stale status text. Defaults and device-authoritative draft synchronization remain.

The owner reported an explicit paused state after opening/cancelling exit. The current
pure model does not pause there; no forced-resume workaround was added. Added tests
cover time passing, intentionally paused cancellation and expiration while in exit.
Per-key page/timer diagnostics were added to locate the physical input path; root cause
and acceptance remain pending. Do not describe this symptom as fixed.

Complete gate, Mac universal build/signature/package and 56 actual LVGL scenes pass
(302-character glyph inventory, peak LVGL allocation 19,696 bytes, 100 theme switches).
Merged SHA-256: `da6bc128be2a2dbed57e2a8589f4e78f4fda1007280b58ce4c69c98306afc980`;
ELF: `810380c0f5a48509275f7b7376babffee44c507cf0b81185357875236dc01684`.
App-only 0x10000 update hash verified with matching partition table and clean 40-second
startup. Mac 0.3.2 installed/reconnected; BlackHole and existing shortcut authorization
are ready. Physical input regression and actual pause diagnosis are unverified.
No commit, push or publication. Private logs are excluded from deliverables.

### Owner acceptance follow-up — 2026-11-08

On the same da6bc128 firmware / Mac 0.3.2, the owner confirmed normal behavior
for start -> running countdown -> long DOWN exit page -> wait three seconds ->
short UP Cancel. The timer continues in the exit page and after Cancel.
Device tests: PASS for this running-state path. The earlier unwanted pause
was not reproduced and its original cause remains unknown. No additional
firmware changes or flashing were needed. The bounded serial observation
contained no key events; acceptance is owner-observed, not trace-derived.
Intentionally paused cancellation, speech regression and power remain unverified.

## 2026-11-08 — App entry/exit and warmer semantic themes

Long DOWN enters Pomodoro from the desktop, directly exits initial/next-phase
waiting, and opens confirmation only while running/paused. The long-DOWN rail
changes immediately from stopwatch to an original door-and-arrow mask. One hold
performs one action, with repeated LONG and release-click suppression. Short
DOWN remains Enter; UP voice and settings-page navigation keep their roles.

Light mode uses warm mineral #C9C5B6 instead of near-white. Dark mode uses deep
blue/warm neutrals; teal voice/UP, blue Enter/rest, orange focus and ochre weather
make meaning visible without color alone. No animation/network/persistence
work was added; exit pixels add 256 static bytes and reuse the image object.

Build: PASS, complete ESP-IDF 5.5.3 gate and verified merged/debug bundle.
Host tests: PASS, app toggle/waiting exit, single-hold boundaries, voice/Enter,
active confirmation/deadlines/manual handoff; 302 static Chinese characters;
56 actual LVGL fixtures plus 100 theme switches, peak 19,672 bytes.
Device tests: PASS for application write/hash, matching ELF and clean 40-second
startup; Mac 0.3.2 reconnects with audio/shortcut permission ready and settings
reload succeeds. Same partition bytes, only 0x10000 application written.
Unverified: new physical button flow, both themes on the LCD, speech regression
and battery current. Owner acceptance of the previous image is not this image's
acceptance. Mac 0.3.2 unchanged; no reinstall or permission refresh needed.

Merged SHA-256: `23f597db2c832fb7a6a30b8e0ea1d139847dd648b1bef197faa336e9ef6d09d1`;
ELF: `528ebf9a29856eb8f7de736c8b4086740b22786aa327be8060e77fee8c5cea78`.
Application 3,151,152 bytes, merged 3,216,688 bytes. No commit/push/public Release.

## 2026-11-08 — On-device theme selector

Device settings now include Screen theme below Screen timeout. Short SETTINGS
enters editing; UP/DOWN cycle Dark / Light / Auto, and SETTINGS completes.
Changes apply immediately, save in the existing version-3 snapshot and notify
Mac through config.changed/settings revisions. Mode changes preserve the saved
schedule, brightness and timer. Auto uses the Mac-configured schedule. Failed
local preference saves roll back the displayed model values. Settings still
end at Unpair Mac; network setup/unpairing retain their distinct actions.
The existing Mac 0.3.2 is unchanged and needs no reinstallation or new permission.

Build: **PASS** — complete isolated ESP-IDF 5.5.3 gate; verified merged layout
and matching ELF/MAP archive. App 3,151,488 bytes; merged 3,217,024 bytes.
Host tests: **PASS** — full repository gate, physical-settings navigation,
three-mode cycling in both directions, preserved brightness/volume/timeout and
active deadline, Wi-Fi action and two-step unpair action. Font coverage 306
static Chinese characters. Actual LVGL: 59 scenes and 100 theme transitions;
no extra wrap/LCD overflow or unchanged-frame flush. Peak 19,680 bytes.
Device tests: **PASS, startup scope** — partition bytes match preceding 23f597db;
application-only update at 0x10000 hash verified, no NVS erase. Forty-second
startup matches ELF/app_main without fault markers; serial closed. Mac 0.3.2
was observed reconnected with audio/shortcut permission ready; no UI settings
were changed while the owner was using voice input.
Unverified: physical theme selection, immediate repaint/readability, notification
to Mac, reboot persistence and speech/timer regression on this image. The owner
accepted entry/exit controls on the preceding image; that is not acceptance of
this build. Palette acceptance remains pending. No commit/push/public Release.
Private logs excluded.

Merged SHA-256: `6be2367f7af0c6aa1ec3952359a0a5c3334bdd0e5bbb7b2c2b41c12f0fc7bc91`
Matching ELF: `c6062c39b1d0a7eb60a84735c477ac3ccc0ff1d148556bae117d29c5beefc1e5`
Application SHA-256: `309a662c072416ec9b7e13d13828253108f8b2498f2fc09f866522bea7779b6d`

The merged image is for **0x0** on a blank device or deliberate complete refresh
and may reset saved data. This update used its matching application at **0x10000**
with compatible partitions to preserve settings/Wi-Fi/pairing. App-only is not 0x0.

### Owner follow-up — 2026-11-08

On firmware 6be2367f, the owner confirmed on-device theme switching works.
The owner reported that the light canvas still appears white on the LCD, and
that editing is difficult to distinguish from selection. Palette acceptance
therefore remains open. Mac synchronization was not explicitly confirmed.

## 2026-11-08 — Editing cues and deeper light palette

Editing now adds an accent outline to the active row and an inverted accent
fill with arrows around its value. Theme, brightness, volume and timeout share
the same cues; browse mode retains the unfilled value. Right-side physical key
actions are unchanged. No new animation or periodic/network task was added.

The light canvas changes from #C9C5B6 to deeper, more chromatic #B8B29B, with
matching panel, selection and semantic ink colors. Dark colors and backlight
settings are unchanged. RGB565 host previews show these software colors;
they do not simulate LCD gamma, backlight, ambient light or camera white balance.
No whitening conversion was found in the inspected software display path;
the cause of the owner's previous physical appearance remains unconfirmed.
Mac 0.3.2 and pairing storage are unchanged; no reinstall/authorization needed.

Build: **PASS** — complete ESP-IDF 5.5.3 isolated gate and verified merged/debug
archive. Application 3,152,080 bytes, merged 3,217,616 bytes.
Host tests: **PASS** — full repository gate; 306 static non-ASCII glyphs;
67 actual RGB565 LVGL fixtures and 100 live theme changes. Fixtures include
selected/editing states in both themes and maximum brightness, volume and
timeout. No unexpected wrapping, screen overflow or unchanged-frame flush;
peak LVGL allocation 19,672 bytes. Visual inspection confirms the filled
editable values, arrows and row outlines fit. No physical color measurement.
Device tests: **PASS, write/startup scope** — exact target identity checked;
partition table bytes match installed 6be2367f. Only the compatible application
at 0x10000 was written and its hash verified; NVS was untouched. Bounded
40-second startup matched ELF/app_main without fault markers; port closed.
Unverified: new edit-cue visibility and light palette appearance on the LCD,
Mac settings synchronization, restart persistence, speech/timer regression and
battery current on this image. On preceding 6be2367f the owner confirmed theme
switching, while rejecting its palette/edit visibility; that is not acceptance
of this image. No commit, push or public Release. Private raw logs excluded.

Merged SHA-256: `ca2df30aeb096c0e49132e68a80ff0ee8259802bce72cbffddcaca3ae98d054b`
Matching ELF: `2581dc4dad64c26e08c721ec0832876c7ed0c0c83ade768b218d40fbd1ccc15e`
Application SHA-256: `9554d20f1f267f17dd49773c0caa05599e884a5d59d6fd896463e7ee2d99fc97`

The merged image is for **0x0** on a blank device or deliberate complete refresh
and may reset stored settings. This update used the matching application at
**0x10000** with compatible partitions to preserve Wi-Fi/pairing. Never write
the application-only image at 0x0.

## 2026-11-08 — Four weather pages

The approved continuous canvas now contains current conditions, next six full
hours, air and wind, and three-day forecast. Hourly temperature, rain probability
and precipitation share six columns. Three-day text sits left of 48 px icons.
Current weather includes feels-like, humidity and daily maximum UV. Air includes
explicit US AQI and PM2.5; current wind, gust and daily maximum use national
GB/T 28591-2012 wind levels converted from unrounded m/s. Missing fields show
an em dash, and temperature gaps are not bridged.

Short SETTINGS advances weather pages and holds the selected page until sleep.
Wake returns to current conditions; a sleeping tap only wakes. Long SETTINGS
still opens functions. Automatic rotation is every eight seconds while awake,
and voice freezes it until release. Default 15-second sleep still applies, so
use manual paging to read all four pages. Pomodoro controls are unchanged.
Requests retain the existing fifteen-minute cadence, with twelve cached hours;
page changes perform no network request or Flash write. No Mac reinstall or
authorization is needed. Radio remains deferred.

Build: **PASS** — complete ESP-IDF 5.5.3 gate, merged layout and matching debug
archive verified. Application 3,217,248 bytes;
merged 3,282,784 bytes.
Host tests: **PASS** — complete repository gate; 631 static non-ASCII characters
covered by both CJK fonts; model/parser AddressSanitizer and UBSan checks;
73 real RGB565 LVGL scenes, 100 theme transitions and 100 weather transitions.
Scenes cover both themes, missing/exhausted hours, extreme values and maximum
PM2.5. Label wrapping, screen bounds, custom chart text bounds and unchanged
frame flush assertions passed. Peak LVGL allocation 19,392 bytes. Graph values
are drawn directly to keep allocation within the existing LVGL budget. A real
public API sample was 1,568 bytes within the unchanged 4,095-byte body limit.
Device tests: **PASS, write/startup scope** — exact USB target checked, partition
bytes matched the installed ca2df30a image. Only application 0x10000 was written,
hash verified; NVS was untouched. Forty-second startup matched ELF/app_main,
with no fault markers; serial closed. Startup cache markers: weather=observed, air=observed.
Unverified: physical LCD readability, new button flow, speech/Enter/Pomodoro
regression, long-duration data refresh and battery current on this image.
The software RGB565 render does not simulate LCD gamma, backlight or camera
white balance. Wi-Fi/pairing storage was preserved by write range; user-level
reconnection and physical acceptance remain to be confirmed. No commit, push
or public release. Raw device logs remain private and are not packaged.

Merged SHA-256: `f8047be7c2f3bd773cc4ba9f4ecba2575e8fc80f5ace300675969787c4ee1a20`
Matching ELF: `21ef325571395eb8f38bcfcf45d6dba9057573a558e22bea07efb4116bc4f207`
Application SHA-256: `bdb665dd13b277c2d879dfa0790c43c0c3cda5bacb76c68f94753b28c261a444`

The merged image is flashable from **0x0** for a blank device or deliberate
full refresh, which may reset settings. This update used its matching compatible
application at **0x10000** to preserve Wi-Fi and pairing. Never write the
application-only image at 0x0.

Acceptance: on awake Home, short SETTINGS cycles the four pages; hold the voice
key from the hourly page, release and check recognition, then short DOWN for
Enter. Check sleep/wake returns to current weather and that Pomodoro still
starts, pauses and hands off manually. Check both themes and share a device
photo if any text clips or feels too small.

## 2026-11-08 — UV strength labels

Daily maximum UV retains one decimal and now appends low, moderate, high, very
high or extreme in green, yellow, orange, red or purple, adapted to each theme.
The value 2.3 is labelled low; missing data stays an unclassified muted em dash.
The existing row fits both value and label, including the accepted 50.0 upper
bound, without reducing the 20 px value text. No new widgets, network requests,
periodic tasks, settings or Flash writes were added.

Classification uses the nearest integer UVI and 0–2, 3–5, 6–7, 8–10, 11+ bands.
Only classification rounds to an integer; the displayed source value keeps a
decimal. Rounding reference: [WHO practical guide](https://www.arpansa.gov.au/sites/default/files/uviguide.pdf).

Build: **PASS** — full ESP-IDF 5.5.3 gate; merged image and matching debug archive
verified. Application 3,217,536 bytes.
Host tests: **PASS** — full repository gate; 633 static non-ASCII characters
covered by both CJK fonts; UV band boundary checks and sanitized weather tests;
87 real RGB565 LVGL scenes, 100 theme and 100 weather transitions.
New scenes cover all five bands, maximum accepted UV and missing UV in both
themes. No unexpected wrapping, out-of-bounds text or unchanged-frame flush;
peak LVGL allocation 19,392 bytes.
Device tests: **PASS, write/startup scope** — exact USB target checked, partitions
match installed f8047be7. Only application 0x10000 written, hash verified, NVS
write range excluded. Forty-second boot matches ELF/app_main, no fault markers;
serial port closed. Weather cache marker: True; air cache marker: False.
Unverified: physical LCD label readability/color appearance and speech/timer
regression on this image. Mac app and pairing schema were unchanged. No commit,
push or public release; private raw logs excluded.

Merged SHA-256: `de20ccf8dbcfc39b5ebb7e4f97f7d755d825059c68644541ed334e8615d0a2c0`
Matching ELF: `2eed6a4801dffb5be09870f08243373def179e5691b9933b9c922e59d65f64e1`
Application SHA-256: `59d6e5ba63e4413bf12bef5676f7c975403593139ffd79f52db46249147dc83a`

The merged image can be flashed at **0x0** for a blank device or intentional full
refresh and may reset saved settings. This update instead used its compatible
matching application at **0x10000** to preserve Wi-Fi and pairing. Never write
the application-only image at 0x0. Please check the UV label on the actual LCD.


## 2026-11-08 — Home radio and Mac station management (0.4.0)

The owner approved implementation after design and authorized direct subsequent
firmware updates. Radio now occupies the lower Home module, mutually exclusive
with weather/Pomodoro while voice and Enter remain on Home. SETTINGS tap cycles
the ordered list; DOWN hold stops/exits directly. Pause/resume stays in Functions.
Switching from active Pomodoro uses direct cancel/switch choices without pausing.
Speech releases the HTTP reader/decoder before capture and resumes only the
previous listening intent. An unavailable Mac leaves playback untouched.

Mac has recommendations (the original six URLs), independent regional search,
custom checking, and My Stations with removal/drag/arrows. At most sixteen
entries, including zero, synchronize through authenticated one-station UDP
messages with revision checks, a sixty-second staging timeout, atomic NVS commit,
abort and idempotent commit retries. Reordering retains the current URL and
verification status. Removing the current URL stops it. Existing single-station
preferences migrate without changing Wi-Fi, pairing or the settings blob.
The last selection is persisted when exiting radio, avoiding a write per change.

Custom detection runs on Mac with a twenty-second overall deadline, at most ten
seconds per request, bounded reads (32 KiB or 128 KiB for HLS media), limited
redirects/depth, frame inspection and AudioFileStream packet parsing. It unwraps
M3U/PLS but retains stable HLS playlist URLs. Mac recognition is distinct from
successful device PCM output; playback marks the matching URL, never a stale
list index. HTTP is allowed for existing public radio streams; HTTPS is retained
without an automatic insecure downgrade. Encrypted/fMP4/byte-range HLS is rejected.
Detection does not prove the embedded decoder can play every source.

Power changes: notifications replace ten-millisecond producer/consumer polling;
volume writes occur only when changed. Pause/exit closes the stream and decoder,
then sleeps codec/I2S. Idle control/network waits become 200 ms with the display
on / 1 s off; queues and UDP loopback wake outbound events immediately. Body reads
check cancellation at 200 ms timeouts and tolerate bounded transient timeouts;
connection/DNS latency can still delay preemption and needs weak-network testing.
Three short failed sessions stop playback after 2/10-second backoffs. Wi-Fi loss
waits for an IP event. MIN_MODEM is retained during playback and existing NONE
mode only during speech. The BSP ADC key scan remains at its existing cadence;
DFS/automatic light sleep being configured is not proof of measured sleep/current.

Build: PASS. Complete gate on ESP-IDF 5.5.3, app 3,223,840
bytes, merged 3,289,376 bytes. Archive:
`build/firmware/cafc2424255cb9bd13fb5159cf25e4a482fa76bd9079addde3012208413e5d81/`.
Full SHA-256: `cafc2424255cb9bd13fb5159cf25e4a482fa76bd9079addde3012208413e5d81`.
Matching ELF SHA-256: `c029a116a8215de3442a93e9e0d4f59ea073d2c15bec5814de6abce7ea71cb42`.
Partition hash remains `420931e3f5af072d899b5357c043fd9f283bf165fc9df6113b6d43d82b48a06d`.

Host tests: PASS. Full gate plus final static checks cover new Home radio keys,
timer cancellation, sleep, empty lists, atomic partial/lost/reordered transfers,
NVS failures, timeouts, audio resume intent and the threaded real-session cleanup.
All 657 static non-ASCII glyphs pass both font sizes. 105 real LVGL scenes,
100 theme and 100 weather transitions pass; peak observed LVGL usage 19,416
bytes of the configured 28 KiB pool. Native Mac transfer/empty-list/revision
checks pass. Local generated real MP3/AAC/TS fixtures pass, including live streaming,
relative HLS, redirect, M3U/PLS queries and error rejection. All six original public
presets, the economic-radio HLS fixture and the HTTPS fixture passed Mac detection
on this network; that is not a device playback claim.

The universal arm64/x86_64 app is ad-hoc signed; native tests, signature check and
DMG verification pass. DMG SHA-256:
`ec0d6e77a45dbfdd517531535f54c1b310c4dc145225b3013e44482c3f6127f1`.
Connection/Pomodoro instruction cards are removed; menu-bar lifecycle, pairing
file and no-Keychain behavior remain. The installed prior app was backed up and
quit before replacement. The owner is actively using the new library/search UI;
its draft was left intact. Initial new-process shortcut permission was not ready;
restart/physical voice acceptance remains pending, without resetting TCC.

Device tests: PASS only for a verified application-only write at 0x10000 to the
identified ESP32-C3, forty-second matching-ELF clean boot and subsequent paired
list retrieval. Wi-Fi and pairing were preserved. No weather/air cache update
was observed in that bounded boot window. Raw logs remain private locally.

Unverified: physical radio audibility, voice interruption/resume, button response,
weak-network connection cancellation, device memory over long playback, every
custom/directory station, rare dynamic name glyphs, current/battery life, Intel
execution and clean-machine installation. The owner has been asked to exercise
play/voice/resume. No commit, push or public release was performed.


## 2026-11-08 — Radio playback failure and memory correction

The first radio build (`cafc2424`) failed physical playback acceptance: the user
reported that every tested station stayed at connecting and then failed. Mac
stream detection did not establish embedded playback compatibility. A diagnostic
build (`a3f0ae96`, matching ELF `3213702f13fb3cea71884252189e452b433bed0e2f55cff87558f3e949167330`)
received direct MP3 data but the real decoder returned `ESP_AUDIO_ERR_MEM_LACK`.
Session startup had about 60 KiB free, a largest block of 44 KiB, and reserved
30 KiB of playback storage before the decoder's own allocations. HLS reached
its parser but also logged an AAC allocation failure. There was no panic.

The controller queue previously embedded a 1,537-byte JSON array in all twelve
slots, including key events. Compact messages retain twelve slots but own a
bounded, exact-length JSON allocation only while queued/processed. Producer
allocation or full-queue failure frees the payload; the controller releases it
after parsing. Removing the oversized static key message also returns RAM.
This reclaims about 19 KiB of permanent internal memory on ESP32-C3 without
reducing the reader FIFO or increasing CPU polling. Decoder memory failures now
stop without repeated reconnect backoff and retain the specific error. Initial
HTTP connection/header timeout is restored to the reference's six seconds;
body reads still check cancellation at short timeout boundaries. DNS/connect
voice-preemption latency remains a physical test item.

Build: PASS. Full ESP-IDF 5.5.3 gate; application 3,224,368 bytes; merged image
3,289,904 bytes. Verified archive:
`build/firmware/3e0160cc049ed454104eeafab9c6a6b8da4083370825c1352c4fa28cd93cef48/`.
Full SHA-256: `3e0160cc049ed454104eeafab9c6a6b8da4083370825c1352c4fa28cd93cef48`.
Matching ELF: `20cd9f9ec872032548963eb086c66bfd1578be14b65df0e702fd72268e631cb4`.

Host tests: PASS. Added actual-message ownership/bounds/allocation/full-queue
fault checks and an audio-worker memory-failure stop test. Reader/decoder
cleanup tests and all prior tests passed; both fonts cover 659 static non-ASCII
characters.

Device tests: FAIL for the initial release, with allocation failure reproduced
on the diagnostic image. The memory correction was written to the compatible
application partition only, with Wi-Fi/pairing retained; playback acceptance is
pending the user's next test. Mac 0.4.0 is unchanged by this correction.

Unverified: corrected physical audibility, interruption/resume, all codecs,
long-session memory stability, actual current and battery runtime. No commit,
push or public release was performed.

Follow-up: `3e0160cc` produced 44.1/48 kHz PCM, but the reader stopped with
a network-error result after brief playback. The user confirmed long stalls;
physical playback remains FAIL. A same-UA direct Mac request received 159,744
bytes over twelve seconds. The installed reader-diagnostic image is now
`c54b2bf8b946650594fae5341eaddb03ac5b0efcaee483f9a21847129b0851ec`,
with matching ELF
`f44994d32f18156273b28d234e941d151b111f44f6418eb0b6fcd4e2b8be1f92`.
The full gate and matching startup passed. It adds received-byte and HTTP
read-exit diagnostics; this is not a completed playback fix. A bounded local
serial observation is waiting for the user's next reproduction.

## 2026-11-08 — Whole-device RAM budget

The reader diagnostic also reproduced fragmentation: about 80,000 bytes free,
largest block 29,696 bytes, old workspace request 30 KiB. The session now opens
the actual MP3/AAC frame decoder before playback buffers. A single-owner factory
lets the simple parser adopt the same decoder; FIFO/input/output/reader-stack
use independent allocations. No dummy reservation, second decoder or smaller
FIFO is used. Tests cover adopted/unused handles, registration/open failure and
every partial workspace allocation failure.

The BSP draw height is configurable, retaining its generic 40-line default.
Vibe uses 20 lines (9,600 bytes) and a 26 KiB LVGL pool, reclaiming 11,648 bytes.
A 24 KiB pool failed weather text rendering and was not flashed. The logical
19 KiB peak excludes allocator overhead. The 26 KiB pool passed 105 scenes and
100 theme/weather transitions; observed logical peak 19,384 bytes.

Installed full SHA-256:
9f001abab2c5723c6e386390885232dffefc33113f0408ebf9c59505fd5336fa.
Matching ELF:
d7f93c4681b50b8a2beca392d5bab693b7f9183de5be1c3021f203bf4bd2019c.
Build: PASS, full gate; app 3,226,224 bytes, merged 3,291,760 bytes.
Host tests: PASS, including 40/20-line BSP checks, 659 glyphs, memory/factory
fault tests and the 26 KiB real LVGL regression. Device tests: compatible app
write/hash and matching boot PASS; Wi-Fi/pairing retained; sustained playback
still FAIL/pending acceptance on this image.

Five persistent application/LVGL stacks reserve 41 KiB. Capture PCM reserves
3,200 bytes; the active station list about 4.9 KiB. Wi-Fi/TCP/system services and
transient TLS/JSON/codec resources add to those costs. Fonts/artwork principally
reside in Flash. Logs observed 87,004 bytes free/largest 73,728 shortly after boot,
later 85,748/largest 65,536, and a minimum free heap of 30,796 during weather work.
The weather stack has already used about 5.5 KiB of its 8 KiB allocation. Periodic
stack margins cover five persistent tasks; idle audio measurements cannot justify
shrinking that stack. Stack reductions, capture-buffer reuse, strict weather/audio
release handshakes, all-codec playback, long-run memory and current are unverified.
No commit, push or public release was performed.

## 2026-11-08 — Stream timing and bounded receive recovery

The user confirmed keyboard/device voice input and radio interruption/resume
after closing Trilium. The shortcut remains left Control+Option+V, a normal
global combination that other applications can also register. Chrome had held
secure keyboard input earlier; quitting it cleared that state but did not by
itself resolve the symptom, so it is not established as the final cause.

The 600-second RAM observation on `9f001aba` ended around 24,000 bytes free,
largest block 7,680, minimum free 12,696. Repeated capture interruptions returned
session-start free heap to about 91 KB. Audio stack usage reached about 5,376
bytes; its idle margin was not representative. This does not establish a
long-run leak-free result.

Diagnostic image `230df9fc` measured 8–9 ms maximum MP3 decode time during a
90-second normal segment without FIFO starvation. Another segment stopped
receiving data, exhausted the FIFO and timed out. The user heard stutters.
Some TS/AAC stations also reproduced decoder allocation failure; that separate
problem is not fixed and Mac probing is not a device-compatibility guarantee.

Current full SHA-256:
acaa571ba374156903901b9818fc941bd4047b214a4ad3dbce5f96e58874c2b2.
Matching ELF:
39116113df9e0d63271e3d808cc4642469e7e5529770a50072c0ef9324e999bd.
Build: PASS; full gate and final static/host checks. App 3,227,680 bytes, merged
3,293,216 bytes. Compatible application-only write/hash and matching boot passed;
Wi-Fi/pairing retained. Mac 0.4.0 is unchanged.

Low FIFO may temporarily leave modem sleep, with a six-second policy deadline,
one-second minimum recovery window, a three-quarter-FIFO recovery threshold and
24-second cooldown. Cleanup restores modem sleep before joining the reader.
The first reconnect of a previously playing stream omits the extra two-second
delay; repeated short failures still back off and stop after three attempts.
No FIFO increase or CPU-frequency lock was added. Host tests cover recovery
hysteresis/deadline/cooldown/cleanup, retry timing and existing audio lifecycles.
Cancellation is no longer labelled as a network error.

Device tests: FAIL for sustained radio acceptance. The user reports some
stations are fine and some still stutter; specific station names are pending.
No fixed-station before/after comparison, recovery-power effectiveness or
current/battery improvement is established. All-codec playback, weakest-link
identification and long-session stability remain unverified. No commit, push
or public release was performed.

## 2026-11-08 — Reallocate MP3 buffering instead of assuming a power fix

The user prioritizes stable playback over power savings and asked to rely on
logs rather than further station-name collection. A recorded data gap was
about 7.7 seconds; 16 KiB covers only about two seconds at 64 kbit/s. Decode time
was 8–9 ms in normal segments. This establishes FIFO starvation, not whether
the upstream service, WAN, AP or power saving caused the underlying data gap.
The temporary radio Wi-Fi wake policy was withdrawn because it was not shown
to help. Modem sleep remains enabled during radio playback.

MP3 now uses three separate 8 KiB FIFO blocks (24 KiB total) and a 4,608-byte
PCM output buffer, sufficient for one 1,152-sample, 16-bit stereo MP3 frame.
Capture/alarm PCM is allocated only when needed and released before entering
radio playback. Relative to the old MP3 buffers plus persistent 3,200-byte PCM,
the net playback allocation increase is 1,408 bytes, excluding allocator
metadata. Other codecs retain 16 KiB FIFO and 8 KiB output; TS/AAC allocation
failures are not fixed. No task-stack reductions were made.

FIFO positions wrap at twice capacity, supporting non-power-of-two segmented
storage without a 32-bit counter discontinuity. Host tests cover a reference
queue, block boundaries/guards, repeated wraps, concurrent 24 KiB streaming,
every partial workspace allocation failure, full-sized MP3 frames, capture
allocation failure/retry and freeing PCM before radio resumes. The first
established-stream reconnect still omits the extra two-second wait, with
repeated short-failure backoff retained.

Full SHA-256:
1277e6e8764403385dfcb9577ecff63a253ab9443f25d6c5213810559c1e416a.
Matching ELF:
ec4f9a20b216166b3930ccb081db5c83ebb369db675b2f2a4a6e396326e876c2.
Build: PASS, full gate; app 3,227,792 bytes, merged 3,293,328 bytes.
Host tests: PASS, including segmented FIFO and PCM ownership regressions.
Device tests: new-buffer sustained playback acceptance NOT RUN; the previous
version still had stutters. Updating uses only the compatible application
partition, retaining Wi-Fi/pairing. Unverified: new-buffer physical playback,
voice/resume regression, all-codec compatibility, long-run memory, current and
battery life. Three seconds of nominal buffering cannot hide arbitrary outages;
startup/refill delay may also grow. No commit, push or public release.

## 2026-11-08 — Reserve voice and reclaim the weather worker

The accepted priority is guaranteed voice workspace over another 0.4 seconds
of nominal 64 kbit/s radio capacity. The 3,200-byte capture/alarm PCM block is
allocated once at audio initialisation and retained while listening. Initial
allocation/task-creation failures roll back cleanly; later captures do not
allocate that block. Reserving RAM does not keep the microphone powered.

Weather no longer owns a permanently sleeping 8 KiB task. The existing network
worker schedules a single update only when due and allowed. After HTTP/TLS,
response and parsing cleanup, the weather task marks completion and parks.
Another task deletes it, synchronously reclaiming its stack. Audio sets an
exclusion flag under the same mutex as creation, waits for cancellation/cleanup
and reaps the worker before capture/radio. Stack profiling holds that lifecycle
lock across transient task-handle use. Host tests cover cadence/backoff,
creation failure, pre-start/in-request cancellation, stale-city rejection and
100 create/cleanup cycles with allocation sanitizers.

Relative to the previous version, the inactive-weather audio budget recovers
8,192 bytes of stack and reserves 3,200 bytes for voice: a net 4,992 bytes,
excluding task/allocator metadata. MP3 FIFO stays 24 KiB; that headroom is not
immediately spent on a bigger buffer. Radio checks cancellation after connection
and headers and before decoder/workspace allocation. Cancellation remains
cooperative: connection/TLS and weather transport can still wait longer than
body reads. A new log records request-to-capture-ready latency, but no numerical
physical latency result was captured in this observation.

Full SHA-256: 1c5c8ba4b5f66a9c506d6a4b52e4d62a0698b3f287f629d66b0e3c165f02f399.
Matching ELF: 44420a560985bde64cd9cf9528349faa89e622fd3a323a764a9da4b8977ea5bc.
Build: PASS; ESP-IDF 5.5.3 complete gate, app 3,228,656 bytes, merged 3,294,192.
Host tests: PASS; full gate and final static checks, including resident PCM under
later allocation pressure and worker ownership/failure/cancellation tests.

Device tests: compatible app-only write/hash and matching boot passed; Wi-Fi
and pairing retained. The first 120-second window released the worker but did
not update weather/air. The user reported a radio connection failure, which was
not captured. Opening the next bounded monitor restarted the device. That boot
updated weather at 10.5 s, air at 13.2 s, then released the worker at 13.3 s.
Idle free heap was 94,852 bytes (largest 30,720); radio session start 100,456,
playback 30,320 (largest 8,704). A 30-second playback interval had no refill,
maximum decode/PCM gap 8 ms and maximum source read 2,040 ms. The user then
confirmed two voice inputs, text, end, listening resume and exit all worked,
with no noticeable voice-start wait. The monitor is closed.

Unverified: the first connection failure's cause, sustained/all-station radio
stability, TS/AAC allocation compatibility, worst-case cancellation latency,
long-run heap behaviour, current and battery life. This short successful
observation does not close the earlier stutter issue. Radio currently releases
its connection/decoder/FIFO during voice and rebuilds afterwards; buffered audio
is discarded. Retaining radio state for faster resume is a possible measured
follow-up, not implemented by this change.

The root README pair now describes the public product, prerequisites, controls,
installation, defaults, free distribution and limitations. The new
[DESIGN](DESIGN.md) pair is the current interaction/visual/resource contract.
No Mac executable changes, commits, pushes or publishing were performed.

The user subsequently accepted releasing/rebuilding radio during voice and
prioritised a coherent memory policy over faster resume. DESIGN now includes
an ownership/lifetime/failure table, simultaneous-peak budgeting, join-before-free
rules and required allocation-failure/repeated-cleanup checks for future features.


## 2026-11-08 — Twenty stations, search and desktop recovery (0.5.1)

The selected-station ceiling is 20, following the owner's decision to reduce the
initial 40-entry request. Active/staged lists allocate by actual count: a station
is 306 bytes and the header is 12 bytes (9 entries 2,766 bytes; 20 entries 6,132
bytes). Transfer staging is cancelled before audio. The resident 3,200-byte voice
buffer is unchanged. Compact v2 NVS storage writes only actual strings, reads old
v1/fixed-16 blobs, and preserves the active list on allocation or save failure.
Old 16-station firmware cannot read newly saved v2 lists after downgrade.

The client library now has Recommendations / Search / Custom. Search uses the
requested Radio Browser name query, ten results per page, Chinese enabled by
default, hidebroken=true, order=votes and reverse=true. Blank names work;
unchecking Chinese omits language. There is no hidden country/bitrate filter.
Load more advances by ten. Parser/URL/codec limits still reject unsupported data.
The client changes the list and order, not device playback.

Connection/pairing copy now uses computer/client; platform-specific instructions
still name macOS. The root bilingual README introduces the product and links to
manual, design and development documentation, without configuration defaults or
implementation logs. Desktop support remains macOS 13+, universal arm64/x86_64.

Permission readiness uses a main-thread PostEvent preflight snapshot shared with
keyboard/network workers. Returning from requested authorization can reload the
client once, preserving settings and station drafts, form inputs and navigation
in a private one-use recovery file. It closes the UDP listener and waits for the
old process to exit before launching the replacement. No AX fallback can make
PostEvent readiness true; no Keychain/TCC database manipulation is used.

Build: PASS. Complete gate, firmware/merged-image verification and matching archive.
Full SHA-256 `153140ede5c442ec214f3f80f1b2fdef199ed02b4b3940434cf68840c255bcff`;
ELF SHA-256 `2e880f5e51e3e198ea66e7f8a881e48bb4c6d7646794c690fc406cea39ffa5c9`.
Host tests: PASS for the firmware gate (663 non-ASCII static glyphs), twenty-entry
atomic transfer, allocation/persist failure, compact storage/migration and the
actual LVGL layouts/transitions. The universal native client, recovery policy,
private one-use snapshots, protocol and cross-language audio vectors are tested;
x86_64 self-tests run under Rosetta rather than on a physical Intel Mac.
Device tests: twenty-entry save/read/reorder and reboot readback passed on the
preceding radio-equivalent image. Original nine stations/order were restored and
read back after the final compatible application-only flash. Flash hash/startup
ELF matched, no fault markers, Wi-Fi/pairing retained, serial closed. No full-list
physical audio acceptance was performed. The user confirmed their current empty
station list is an unsaved draft; it was restored in the client, not saved over
the device list. Automatic permission reload/window and form preservation were
observed; final permission/voice acceptance is recorded separately below.
Unverified: physical Intel and clean-machine installation, all stations and
long-run playback/heap, radio/voice regression on this exact final image, current
and battery life. No commit, push or public release was performed.

On this development Mac, stale ad-hoc authorization records were cleared for
this bundle only using Apple's `tccutil reset PostEvent` and `reset Accessibility`.
This is not an app startup action and does not grant permission. After enabling
the existing system switch for the already authorized app, returning to the app
automatically reloaded it and showed PostEvent authorized; no manual quit/reopen
was needed. The final app reconnected. The confirmed empty station draft and
Search tab with Chinese checked were restored; the saved device list remains nine.
Actual speech/Enter and long radio playback on this exact image remain unverified.

## 2026-11-08 — Curated radio defaults and compact search row (0.5.2)

The selected fifteen stations and their order now form both desktop
recommendations and the firmware's initial station list. The CNR arts playlist
uses its verified unsigned entry point, rather than embedding transient query
parameters. Existing saved station URLs remain untouched. A saved empty list
still takes precedence over defaults. Default allocation includes a legacy slot
only when it contains a distinct valid station; the normal fifteen-entry list
uses 4,602 bytes. The search row orders the name field, Chinese checkbox and
Search button; the button keeps its intrinsic width and the input fills the
remaining space. Blank-name search and its existing query rules are unchanged.

Build: PASS. Full repository gate and verified merged image, SHA-256
`cc30d621a651f7a1d721fbe5b67a0cdd0e5507fa450f3b8f4f0948488fa1e57a`;
matching ELF `e258058356320f435fac8c3e76f593ceeaa7bfbe47b08355850eef5ad3bc68f9`.
The content-addressed archive is under `build/firmware/` with the full-image hash.
Universal macOS 0.5.2 build, signature and DMG verification passed.
Host tests: PASS. Recommendation/default consistency, bounded catalog fallback,
all existing firmware host checks, 666 non-ASCII glyphs, native Swift tests and
x86_64 Swift tests under Rosetta passed.
Device tests: PASS for the tested scope. The compatible application-only update
at 0x10000 preserved Wi-Fi/pairing and all fifteen station names, URLs and order
(revision 7). Hash/startup ELF matched, no fault markers in the 40-second window,
and the serial port was closed. The final desktop search row was visually
checked with all controls fully visible; the recommendation list and 15/20 saved
list were observed. This development Mac's stale ad-hoc permission records were
refreshed for this app only; it automatically reloaded and showed authorized.
Unverified: physical fresh-device default initialization, every station and
long-running playback, physical voice/Enter regression on this exact build,
physical Intel and clean-machine installation, current and battery life.
No commit, push or public release was performed.


## 2026-11-08 — Automatic LAN client port and quieter keepalives (0.6.0)

The device keeps discovery port 33333. The client binds an OS-selected exclusive
UDP port and uses that one socket for discovery, control and audio. The device
replies to the authenticated beacon's actual source IP/port; ordinary messages
must match both. Endpoint/boot changes clear stale pending work and held keys,
then reload device state without replaying unconfirmed saves. New firmware
accepts old fixed-port clients; update firmware before the dynamic-port client.

Readiness/time now share the existing five-second beacon. Successful outgoing
audio/control refreshes the device heartbeat timestamp. Fallback pings wait seven
seconds, allowing two seconds beyond the normal beacon interval; peer expiry
stays fifteen seconds. No new task or shorter receive polling was introduced.
The existing dynamic frequency/automatic light sleep configuration reported
ESP_OK at startup; Wi-Fi minimum modem sleep remains the idle policy.

Build: PASS. Complete repository gate and archived merged firmware SHA-256
`2e96d32ec45d203d0bf2656b2ed115102e6553531429e60234aa641a82a7b809`;
matching ELF `71a093f572cbe16f40739f605181fe2786e22848a2865ea8096a17b1287e44b6`.
Universal macOS 0.6.0 build, ad-hoc signature, DMG and both native architecture
self-tests passed (x86_64 ran under Rosetta).
Host tests: PASS. Added endpoint/heartbeat boundary tests and real loopback
socket tests with occupied port 33333, combined status, source-port checking and
replacement endpoints; all existing firmware checks and 666 static glyphs passed.
Device tests: PASS for the network scope. Application-only flash at 0x10000,
verified hash/startup ELF, no fault markers in forty seconds, serial closed.
Occupied 33333 did not block dynamic discovery, config read/no-op save or list
readback. Endpoint replacement rejected stale-source commands; bad tokens could
not take over. Replies ignored a deliberately wrong advertised client port.
Legacy fixed-port access worked. Wi-Fi/pairing, settings and fifteen station
names/URLs/order (revision 7) were unchanged. A twelve-second passive window
saw three combined-beacon replies and no redundant pings; stopping beacons
produced a fallback ping. The installed desktop connected and loaded fifteen
stations while another process exclusively held 33333. Its existing keyboard
authorization was refreshed, automatically reloaded and observed ready.
Unverified: physical speech/Enter and radio regression on this exact image,
router outages, actual computer sleep/wake, long LAN soak, physical Intel,
clean-machine installation and electrical current/battery life. No measured
battery improvement is claimed. No commit, push or public release was performed.


## 2026-11-08 — Remembered Wi-Fi networks and automatic location selection

Added up to five saved Wi-Fi profiles. On startup/disconnection, try the last
successful profile, then every other saved profile. Each attempt has a fifteen-
second deadline; failed full rounds back off 5/10/20/40/60 seconds. A healthy
link does not scan or hop. The phone portal groups saved/nearby networks, allows
password-free reuse of saved credentials, password replacement and forgetting.
New credentials replace a profile only after connection and successful storage.
Capacity exhaustion requires forgetting an entry; no silent eviction.

The old single-network NVS value migrates to a bounded versioned compact value
under the same key. No stale legacy credential copy remains after migration.
The bank is 487 bytes and the encoded payload at most 491 bytes, excluding NVS
metadata. Temporary bank/codec work uses existing worker stacks; a separate mutex
protects profiles. No extra resident task, stack or polling loop was added.
Reference inspected: passport-os `b70879b3c4be0751cb728b8b0c421dd2d1524fdb`
(saved-profile management and automatic candidate selection); existing BSP,
provisioning portal, partitions and application UI remain the implementation base.
Multiple-computer pairing/switching is explicitly deferred by the user.

Build: PASS. Complete gate and archived merged image SHA-256
`3801b0ff055e940d0cf44fbed5863d6212aaa6608883baf9638c1646db1c7c2c`;
matching ELF `4cd72900fb48cf57745912b513374fc1dc180ebf6f04500aeaa7279d1bebea79`.
The installed universal macOS 0.6.0 client is unchanged from the preceding
validated build and is compatible with this firmware.
Host tests: PASS. Compact encode/decode, all truncations, malformed records,
maximum lengths, full capacity, alias-safe same-name edits, forget/reindex,
failed-save rollback, redundant-write suppression, every saved candidate,
retry deadline and capped backoff passed under address/undefined sanitizers.
All existing repository checks and 666 static non-ASCII glyphs passed.
A browser test of the actual portal with non-private fixtures verified grouping,
saved-network selection, password editing and successful saved-credential reuse;
the request contained useSaved=true without a password value.
Device tests: PASS for the exercised scope. Application-only flash at 0x10000
preserved Wi-Fi, computer pairing, settings and fifteen station names/URLs/order.
Write hash/startup ELF matched, the forty-second window had no fault markers,
weather/air updated, and the serial port closed. Existing Wi-Fi automatically
connected on boot; the network task had 5,796 unused stack bytes in the sampled
startup path. The client reconnected with keyboard authorization ready.
The user confirmed the phone listed the original saved network, reused its
password and returned Home automatically. The user also confirmed voice start,
transcription, release-to-end and Enter all worked on this final image.
Unverified: actual switching between two configured locations on startup or
outage (the user will test at home/work), hidden networks, wrong-password/full-
storage device failures, long playback/heap soak, physical Intel/clean-machine
installation and electrical current/battery life. The earlier occupied-port
acceptance used the preceding network-equivalent image. No measured battery
improvement is claimed. No commit, push or public release was performed.


## 2026-11-08 — Agent reminders, first device test (0.7.0)

Implemented seven adapters, a loopback helper with a system-assigned port and an
account-private endpoint, authenticated device delivery with bounded retries,
and a fixed-size alert queue. Helper invocations reread the endpoint; they do
not depend on port 33333. No extra firmware task, socket, timer or PCM buffer.
Compared with the preceding multi-Wi-Fi ELF, DRAM BSS increased by 280 bytes;
the queue state itself is 272 bytes. IRAM and DRAM data sections are unchanged.

Build: PASS. Complete firmware gate and archive verification; full image SHA-256
`dfc1972dbfde034b8d681d761d84c9e69a864ddbfbf1f3852e68eea2c56f96aa`,
matching ELF `d76db1c18388097065fdc020e443927d7e566f403b948367c4d9acab08193c74`.
macOS 0.7.0 universal app, signature and DMG verification passed.
Host tests: PASS. Fixed queue, seven event mappings, native plugins, selective
config merge/removal, helper subprocess following endpoint replacement, and
existing regressions. Agent tests also passed under Rosetta x86_64.
Device tests: PASS within observed scope. Application-only update at 0x10000
verified its hash; compatible partition layout, Wi-Fi and pairing retained.
Forty-second serial window matched the ELF and showed no fault markers. The
user confirmed the transport test woke the display, showed completion and rang.
A real Claude Code 2.1.309 print-mode session completed; its native Stop hook
exited successfully and the client showed the resulting completion event.
Unverified: other real Agent completions, interactive approvals/questions, DSH
desktop installation, sustained radio/voice regression and measured power/stack
peaks. Do not equate adapter host tests or the transport button with real-session
coverage. No Git commit, push or public release.

Latest user direction: prioritize actual Agent tests over voice-permission
refresh. Limit alerts to cases requiring human attention; review the four event
classes against agent-notify. The initial Codex integration has eight callback
registrations, including silent lifecycle helpers; simplify unnecessary ones
and improve identification before requesting another trust review. The app
currently holds an unsaved radio-list draft, so preserve it during any update.


## Agent callback simplification — client 0.7.2

User confirmed the earlier real Codex desktop completion woke/rang the toy.
Following explicit direction, removed silent cancellation hooks and the 1 s /
150 ms candidate delays. All seven adapters were reviewed: Codex 5→3 callbacks,
Claude Code 8→4, Qwen Code 8→3, Gemini CLI 5→2, ZCode 4→2, OpenCode 9→4,
DSH 5→4. Claude's Notification callback only accepts MCP form/URL requests;
generic idle and duplicate delayed permission notifications are ignored.
OpenCode ignores resolved/started/cancelled events before session lookup. DSH
keeps the reference waterfall continuation contract and root-agent filtering.
Bounded duplicate filtering, expiry and transport retries remain. No device
code/UI changes or additional device memory; no flash was performed.

With user confirmation that drafts were saved, installed the universal 0.7.2
app and refreshed its helper and prepared native plugins. Replaced owned
Codex/Claude hooks in the actual account configuration, preserving unrelated
entries and private backups. Other command adapters were not installed without
a prior integration. Active native Agent servers must reload updated plugins.

Build: PASS. Universal app, ad-hoc signature, DMG, full repository gate and
archive verification. The gate rebuilt full image
`631d7c5d4cab1efec28f18b3cb3f13c4ac71bbb76d1ac5254d2949a0c2b118f1`,
matching ELF `e479ca8b972a0113640dcb5b4591fc78bedb8021d37b1c5a90d2a857bcc5b3ab`;
this rebuild was not flashed. The device retains the previously verified image.
Host tests: PASS. Migration/removal for all five hook formats, immediate
delivery, duplicate filtering, ignored lifecycle/idle, native listener reduction
and existing regression tests. Agent tests also passed under Rosetta x86_64.
Device tests: PASS for the observed event path. A real Codex CLI 0.166.0
read-only, ephemeral test turn completed successfully with no hook-trust warning;
the running 0.7.2 app displayed Codex completion and device acknowledgement.
Unverified: fresh physical wake/chime checks for this exact client, approvals/
questions for all Agents, native servers after reload, ZCode/Gemini installation,
DSH desktop installation, radio stability and measured power. Voice authorization
refresh remains deferred at user request. No commit, push or public release.

User separately requested UI design discussion before development and explicit
memory impact disclosure. This referred to Agent notifications on the toy;
the approved compact-overlay implementation is recorded below. No UI edits
were made in the callback simplification itself.

## Compact Agent overlay

Implemented the approved 204 x 80 px compact notice in the existing LVGL
post-draw pass, with semantic badge, 16 px Agent name, 20 px action and both
themes. The uncovered page stays intact and continues updating. Notices close
after five seconds. Any physical press dismisses the current notice and queued
backlog before delivering that same gesture through the existing input path.
Voice/provisioning deferral, short burst replacement, expiry and deduplication
remain bounded. No popup objects, frame snapshot, font asset, task or timer;
the 26 KiB LVGL pool and audio/radio memory policy remain unchanged.

Target map: packed state is four bytes; BSS grows eight bytes including
alignment, with initialized DRAM unchanged. Matched full-frame host measurements
show at most 376 extra peak draw bytes inside the existing pool; highest tested
frame payload peak is 19,832 bytes. This is host instrumentation, not device
heap attribution. Two themes, four events, ten underlying pages and 2,000
show/hide cycles passed exact outside-pixel, full restoration, object-count and
retained-allocation assertions. Existing live theme/weather transition checks,
glyph coverage and sanitized queue timing/dismissal tests also passed.

Build: PASS. ESP-IDF 5.5.3 complete gate and verified content-addressed archive.
Full image SHA256:
`c1c5904269e3cfd14e8d9b441e668afc4190138f7989fd98e9533711f0dd1bd5`.
Matching ELF SHA256:
`43bea1b063ea22e66848da175cb6a954cc382909ad75fa9d7258901a219d10c8`.
Archive: `build/firmware/c1c5904269e3cfd14e8d9b441e668afc4190138f7989fd98e9533711f0dd1bd5/`.
Host tests: PASS. Complete gate plus actual LVGL rendering/stress checks above.
Device tests: PASS for application-only flash hash verification, boot identity
and 90-second startup observation. With the partition table matching the
previously running archive, wrote only the verified app at 0x10000 under the
owner's standing authorization; NVS was not overwritten. The client displayed
the device online before the computer auto-locked. Observed free heap 94,352 B,
largest block 43,008 B, minimum 30,736 B and LVGL task stack headroom 3,436 B;
no crash or allocation failure in that window. An existing weather HTTP header
buffer diagnostic still appeared; this UI change does not fix network/radio
issues or prove a comparable heap/power improvement.
User acceptance: PASS for physical popup appearance, approximately five-second
automatic dismissal, and pressing a device key dismissing the overlay while
continuing that key's original function. The user confirmed these checks after
the existing Agent helper submitted the test event.
Unverified: exhaustive voice/Enter/settings gesture coverage, Pomodoro
continuity, radio/chime recovery and measured power. The acceptance reply did
not separately assess the chime. No desktop reinstall, commit, push or release.

## Portal scanning after changing location

The user reported that at a new location the phone portal showed saved profiles
but no nearby networks. Inspection confirmed that automatic saved-profile
connection attempts continued while the portal called the blocking scan API.
ESP-IDF 5.5.3 documents that scanning while connecting returns
ESP_ERR_WIFI_STATE. The old handler returned a successful empty list for this
and other errors, while the page's saved-list message concealed the failure.
This is a confirmed code defect; no matching device error log was captured to
attribute the reported instance conclusively.

Opening setup now cancels an unfinished automatic attempt when disconnected,
pauses background saved-profile retries and ignores their delayed failure
callbacks. Only explicitly submitted credentials are tested during setup;
closing setup resets normal automatic retry. Boot still tries all known
profiles before offering the hotspot. Healthy STA links can remain connected.
Scan busy/failure/OOM receive distinct errors; the phone preserves saved entries
and distinguishes actual empty 2.4 GHz results. Refresh can recover normally.
Results are drained one at a time, with the existing sixteen-result cap and
driver-list cleanup on every post-scan exit. The duplicate record-array heap
allocation was removed. Initialized DRAM (13,980 B) and BSS (54,768 B) match the
previous image; no resident task, buffer or scan cache was added.

Build: PASS. Complete ESP-IDF 5.5.3 gate and archive verification. Full SHA256
`97689b4b5562b2f2e69e15f7f13ecc23c8a5c81549ff544e5f431d2e45b4a022`;
matching ELF `688ea9bfb6d315516d929be1bd196db4e0919c46b58529e585432fb6bbc03b76`.
Archive: `build/firmware/97689b4b5562b2f2e69e15f7f13ecc23c8a5c81549ff544e5f431d2e45b4a022/`.
Host tests: PASS. Production scan handler with driver/HTTP stubs, busy/read
errors versus true empty results, every JSON allocation/serialization failure,
bounded results and repeated cleanup; actual inline portal script checks for
saved-list retention, error messages, refresh recovery and manual entry.
Device tests: PASS for authorized app-only flash verification and startup ELF
identity. The first attempt stopped before writing because the USB port had
disappeared. After the user reconnected the device, rediscovered its Espressif
USB serial unit at /dev/cu.usbmodem2101, reverified the archive and matching
partition table, then wrote only the archived app at 0x10000; NVS was not
overwritten. Startup reported ELF prefix 688ea9bfb, matching this archive.
The bounded 90-second log window showed no crash, allocator failure or portal
scan error. The final observed free heap was 99,988 B, largest block 31,744 B,
minimum 31,696 B and LVGL stack headroom 3,420 B; these startup samples are not
worst-case scan measurements. The serial monitor was closed afterwards.
Unverified: new-location scan/connection, saved-network boot/retry after leaving
setup, captive portal recovery and target scan heap/stack peaks. No commit,
push, desktop reinstall or public release.

## 2026-11-09 — Standby wake budget and saved-network startup recovery

The owner measured a complete discharge in about six hours from the nominal
500 mAh battery, without radio, with roughly five minutes of display use and
fewer than twenty alert sounds. 500/6 is approximately 83 mA nominal average,
not an instrument measurement. The previous delivered ELF confirmed an 8 ms
automatic-Light-sleep threshold while button and LVGL timers each woke every
5 ms. Blanking only set PWM duty to zero; LCD Sleep In was not sent.

Buttons now poll every 20 ms with one-sample debounce, retaining the 180/500 ms
gesture windows. A compile-time assertion protects the idle threshold/window
relationship. LVGL reads monotonic time on demand; its port maintenance tick
is 1 s and visible refresh remains 20 ms. Reversible screen standby pauses
the display refresh timer, drains pending SPI transfers through panel commands,
and sends Sleep In. Wake restores the LCD, refresh and backlight; incomplete
transitions are retryable. This deliberately avoids global lvgl_port_stop:
LVGL 9.5 returns 1 ms while disabled, causing port 2.9.0 to busy-poll. No managed
dependency was patched. Voice/alert work is queued before blocking LCD commands.
An awake-display lock protects the default LEDC PWM from Light-sleep gating;
an enumerated USB-host lock preserves C3 flashing/serial access. Battery-only
acceptance requires unplugging USB. Idle Wi-Fi and notification intervals,
resident voice storage, radio buffering and timer deadlines are unchanged.

The owner accepted two screen-off voice/wake/sleep cycles on 9e22e877, then
reported unexpected automatic provisioning at startup. That boot log showed
association followed by another attempt and a setup AP. Publish the replacement
SSID before disconnecting, reject delayed failures for an old SSID or an
already-associated replacement, and recheck association in the network worker.
Select the retry profile after applying timeouts. A completed earlier failed
round cannot open automatic setup during a pending attempt or DHCP association.
Credentials/NVS remain unchanged. Diagnostics log numeric reasons/profile slots,
never SSIDs/passwords. DESIGN and its translation record these power/lifecycle
invariants and the distinction between SOC trends and measured electrical current.

Build: PASS. Complete isolated ESP-IDF 5.5.3 gate and archive verification.
Application 3,249,216 B; merged 3,314,752 B. Archive:
`build/firmware/98243dc94b43a86d2602a5a5deff961880ef5e48476cddefc3100d129f3c6c3b/`.
Full SHA256 `98243dc94b43a86d2602a5a5deff961880ef5e48476cddefc3100d129f3c6c3b`;
matching ELF `987302f08d1da562f7da393e1181ecbb685aca4ef5394d533cab7e0b85cf3a30`.
Initialized DRAM 13,980 B and BSS 54,768 B match the prior delivered firmware.
No new task, framebuffer, audio buffer or history ring. Two PM locks request
32 B each on C3, plus allocator overhead; these are not a KiB-scale workspace.
Host tests: PASS. Actual BSP standby/recovery code with panel/clock stubs covers
64 repeated cycles, lock failure, Sleep In/Out/display-enable failure, retry,
idempotence and monotonic time without frequent tick interrupts. Saved-network
tests cover stale/different/maximum-length SSIDs, a valid new association and
pending attempts past the automatic-setup deadline. Existing voice/radio,
portal, allocation-failure, glyph and protocol checks also pass.
Device tests: PASS for app-only flash verification with identical partitions,
matching boot ELF, automatic saved-network connection and no setup AP in the
bounded 85-second observation. Association first timed out (reason 4), another
saved site was absent (201), then retry acquired IP at about 11.6 s. LCD sleep
succeeded and no panic/heap corruption was observed. The owner confirmed Home
without manual Wi-Fi selection. Final observed heap free 94,288 B, largest
32,768 B, minimum 30,140 B and LVGL stack headroom 3,416 B; these are not worst
radio/scan measurements. Serial observation was closed.
Unverified: actual battery-side current, battery-only Light-sleep residency and
new full-discharge runtime; on the final image, voice after battery-only sleep,
radio regression and all-site boot/outage/portal switching. USB observation is
not proof of battery savings. No power-history desktop feature was added, and
no commit, push or public release was performed.

## 2026-11-09 — Battery percentage and manual weather pages

Home now shows SOC immediately left of the battery icon, with room for 100%.
The existing label and gauge are reused: sample at boot/wake and once per minute
while visible; no blank-screen polling. Weather no longer rotates automatically;
short SETTINGS advances one of four cached pages, and wake resets to current
weather. No new task, history buffer, network request or Flash write is needed.

Build / Host tests: PASS, complete gate. Full SHA256
`624bec6ae643f4c770edc21dafe4d92dfb3962927cb515d6935f7f84ceccd402`;
matching ELF `32749608370c88d0a5beab37f1426c43b2cfadcc9abaab63d7570060d3b1302e`.
Device tests: app-only flash and owner acceptance PASS for visible/unobstructed
percentage, manual page change and no automatic change after ten seconds.
Unverified: SOC calibration and actual current. The later 89% plateau and
89→84% battery-only drop are gauge observations, not calibrated energy readings.

## 2026-11-10 — Event-driven standby, negotiated heartbeat and sleep diagnostics

The owner authorized implementation after reporting a battery-only percentage
drop and asking whether sleep modes and periodic heartbeat could be improved.
All three ADC-ladder buttons now use the pinned button component's idle power
save interface: one GPIO0 low-level ISR restarts scanning on a key press; idle
stops the 20 ms poll. Analog sampling and digital wake alternate on the same
owned ADC/pin, without added pulls. Failed arming resumes polling; setup failure
retains polling. The handler is removed before ADC teardown. Physical validation
of all three keys is required; host stubs cannot establish voltage thresholds.

Controller/network waits are up to thirty seconds while blank, interrupted by
queue/socket events; timer completion caps the controller wait at its deadline.
Provisioning/reconnect/recording retain short waits. Packets are timestamped
after the blocking receive. LVGL maintenance and idle waits are thirty seconds,
with the same monotonic clock, 20 ms visible refresh and reversible LCD sleep.
RSSI is read only while visible; memory logs only while a USB host is connected.
Weather refresh intervals, voice resident buffers, radio ownership and MIN_MODEM
Wi-Fi policy are unchanged. No deep sleep is introduced because it would lose
immediate LAN notifications.

Matching peers negotiate a thirty-second combined beacon and ninety-second
expiry; discovery stays two seconds, and old peers retain 5/15 seconds. Readiness
changes, voice, Enter and Agent reminders send immediately. Normal outbound
traffic substitutes for fallback ping traffic. A slower passive offline result
is the deliberate compatibility/power tradeoff, not a delay on delivered events.

Authenticated manual power.get/reset returns aggregate successful/rejected
Light-sleep attempts, successful-call duration (including entry/exit overhead),
blank-only totals, longest call, USB/key-wake state, fresh gauge data and bounded
PM lock history. It uses the IDF PM exit callback plus the pinned SDK sleep
context, excluding skipped/failed attempts from sleep duration. No new timer,
task, history ring or Flash writes. Mac 0.8.0 exposes a manual disclosure under
Connection and Voice. Requests do not light the display; reading itself affects
CPU/Wi-Fi activity. Session reset does not reset boot-time PM lock history.

Memory versus 624bec6a: initialized DRAM remains 13,980 B; BSS 54,752→54,928 B
(+176), IRAM text 100,558→101,462 B (+904), about 1.1 KiB additional static RAM.
DWARF confirms PM profiling grows each C3 lock 32→56 B (+24 per allocated lock).
GPIO ISR setup needs a 22-entry callback table (176 B) plus interrupt allocator
overhead, and PM exit registration needs one small node. Lock text uses a bounded
temporary stack workspace with transient stdio/cJSON allocations, not a resident
log buffer. Dynamic heap/stack peaks require target measurements.

Build: PASS. Complete isolated ESP-IDF 5.5.3 gate and archive verification.
App 3,258,256 B; merged 3,323,792 B. Full SHA256
`acbf7a820463f9acf5191a423cfb00191d22fe609b85e16c34db7d4ee517c564`;
matching ELF `783d31778d8bc315eed259c34ca37cda8309a49b0885a722b532b11666663e1e`.
Archive: `build/firmware/acbf7a820463f9acf5191a423cfb00191d22fe609b85e16c34db7d4ee517c564/`.
Mac 0.8.0 universal arm64/x86_64 build, native self-tests, voice vectors, Agent
tests, ad-hoc signature verification and DMG verification PASS.
Host tests: PASS. Repeated ADC/idle-IRQ handoff for three keys; arming/setup
failure and teardown; LVGL sparse maintenance; exact timer deadline; old/new
heartbeat timing, endpoint changes and traffic-aware fallback. Actual power.c
with SDK stubs covers skipped calls, failed calls, successful-only 90% residency,
reset while blank, USB status and bounded/truncated dumps with 100 lock rows.
Diagnostic counter formatting rejects nonfinite/oversized values safely.
Device tests: PASS for authorized app-only flash/write-hash verification,
matching boot ELF prefix 783d31778, PM/diagnostics initialization (ESP_OK), LCD
sleep and desktop reconnection. Rediscovered the same Espressif USB serial unit
at /dev/cu.usbmodem1101 and checked that the archived partition table matched
624bec6a; only 0x10000 app bytes were written. NVS was not overwritten. The
85-second bounded observation found no panic/assert/heap-corruption markers,
and the monitor was closed. A startup sample showed free heap 41,244 B, largest
29,696 B and minimum 30,156 B; this was not an idle/radio worst-case measurement.

After the owner confirmed no unsaved drafts, Mac 0.8.0 was installed and started.
The previous app was retained locally for recovery. Existing pairing reconnected.
The owner completed system authentication to refresh the existing shortcut grant;
the app automatically reloaded it and showed authorized, without a manual restart.
The manual diagnostics request succeeded: idle key wake enabled, display lock
released while blank, USB-host lock active, five successful short startup sleep
calls and zero rejects. Blank residency was 0.3% while USB was attached, not a
battery-only result. The diagnostics disclosure was left open for acceptance.

The owner reported 89→21% from 22:00 to 08:00 (ten hours) on the previous
firmware. Preserve this as a gauge-based comparison session, not a calibrated
current/capacity result or acceptance of the new image's runtime.

Battery-only acceptance sample supplied by the owner after resetting the session:
762.6 s elapsed and blank, 568.3 s successful-call Light-sleep (74.5%), 8,257
successful calls, one rejection, longest displayed 0.1 s, idle key wake enabled,
40% SOC / 3,788 mV. Display, I2S, I2C and USB NO_LIGHT_SLEEP locks were inactive
at readout. Wi-Fi APB and RTOS CPU locks were active in the snapshot; the request
itself wakes both, so this is not evidence they remained acquired throughout
the test. All held_ms totals are since boot, not since the session reset.
The sample establishes functioning battery-only automatic Light-sleep, not
current draw, savings versus the previous image or full-discharge endurance.

Calculated successful-call mean is 68.8 ms, with 10.83 successful calls/s and
194.3 s outside those calls. Existing matching boot logs report AP beacon
102,400 us, DTIM 1 and MIN_MODEM. IDF 5.5.3 documents that MIN_MODEM wakes every
DTIM; this makes Wi-Fi reception a strong candidate for the short cycles,
but no wake-source breakdown establishes every cause or attributes the whole
25.5% remainder. A future MAX_MODEM/listen-interval comparison must preserve
notification reliability and foreground voice/radio policy. No firmware was
modified or flashed for this analysis.
Unverified: physical ADC wake/gestures on all keys, repeatability of sleep residency,
wake-to-voice/Enter/Agent latency, radio regression, timer alarm deadline on a
blank screen, actual battery-side current and full discharge runtime. USB-host
logs cannot prove Light-sleep savings. No commit, push or public release.

## 2026-11-10 — Shorter negotiated heartbeat, client 0.8.1

The owner accepted the voice, Agent reminder and Pomodoro checks, then requested
faster passive connection feedback. Client 0.8.1 requests a ten-second combined
status beacon and uses a thirty-second peer timeout. The installed firmware
already accepts intervals from five to thirty seconds; its advertised value 30
is the supported maximum, not a requirement to choose thirty seconds. Legacy
peers retain 5/15 seconds. Delivered events still send immediately. This interval
negotiation is separate from Wi-Fi beacon/DTIM reception and sleep timing.
Shorter heartbeats add traffic; their energy difference has not been measured.

Only desktop production code changed. No firmware was flashed and no device RAM
was added. The device retains acbf7a82 with matching ELF 783d31778. Further Wi-Fi
power-policy changes are deferred until a battery-only runtime of this firmware
is available; 74.5% successful sleep-call residency cannot establish energy
savings, a wake-source breakdown or the remaining optimization margin.

Build: PASS. Complete isolated gate produced verified validation-only archive
`build/firmware/2b8f24d53ddee70da2fc2e0397d3d9f6fd3f97e33fec909f03be3a8bea9d0199/`.
Mac 0.8.1 universal arm64/x86_64 build, signature and DMG verification PASS.
Host tests: PASS, including negotiated 10/30 timing, legacy timing, real UDP
port collision/endpoint replacement, Agent integration and voice wire vectors.
Device tests: PASS for automatic LAN reconnection after installing 0.8.1;
refreshing the existing shortcut grant was detected automatically by the app.
Unverified: actual passive offline timing, post-update physical voice/radio
regression, overnight endurance and battery-side current. Previous voice, Agent
and Pomodoro acceptance applies to the unchanged firmware. No commit, push or
public release.


## 2026-11-10 — Radio SRAM recovery and decoder input ownership

Kept the 3,200-byte resident voice PCM and the existing UI, DMA, task-stack and
radio FIFO capacities. Moved optional FreeRTOS/ring-buffer functions to Flash,
disabled the Wi-Fi IRAM speed optimizations, enabled dynamic TLS buffers and
released handshake configuration/peer certificates. Certificate validation stays
enabled. Registered only the accepted MP3/AAC codecs and TS container. Failed
partial registrations now roll back; TS first-frame decoding precedes FIFO and
reader allocation, preserving its PCM, remaining input and ICY state.

Compared with the installed acbf7a82 baseline, static shared SRAM use fell from
170,370 to 136,926 bytes: **33,444 bytes (32.66 KiB) recovered**. IRAM text fell
from 101,462 to 68,250 bytes; initialized data fell by 232 bytes and BSS remained
54,928 bytes. These are linker measurements, not playback free heap or current.
An initial device trial had about 129 KiB free before source allocation and
about 88 KiB in its largest block, but still reproduced AAC decode errors, TLS
allocation failures on one HTTPS/HLS stream and source stalls. That trial was
not a successful radio acceptance test.

The decoder loop previously dropped remaining input when a call consumed no
new bytes despite outputting internally buffered PCM. It now resubmits those
bytes, with a bounded failure for no input/output progress. Added regressions
for buffered output during priming and after the reader starts, as well as
partial allocation/registration cleanup and malformed container bounds.

Build: PASS, full isolated gate and verified merged image.
Host tests: PASS, including 26 session modes repeated three times under
ASan/UBSan, staged workspace allocation and every partial-allocation cleanup.
Full image: `f9a903c14d3ca6bd47ce3b7d8f9ba03589112070370acba05bfe9da1c85d571b`.
Matching ELF: `3a4c76eb09c5475839c7c594365d54a021cef3891ac271780854e65848227172`.
Archive: `build/firmware/f9a903c14d3ca6bd47ce3b7d8f9ba03589112070370acba05bfe9da1c85d571b/`.

Device tests: application-only flash at 0x10000 passed write verification; the
partition table is byte-identical and Wi-Fi/pairing/station storage was not
written. Final playback acceptance is still in progress: the user reports
connection failures on the two CNR HLS stations. A direct MP3 session logged
30 seconds with zero refills, but this does not establish broad compatibility.
Unverified: sustained AAC/HLS playback, repeated voice interruption/recovery,
station switching/exit after this change, overnight energy use and battery-side
current. No client update, commit, push or public release.


### Follow-up: measured radio admission budget

The next trial reproduced a TLS receive-record allocation failure: after FIFO
allocation an HTTPS/TS/AAC session could retain only 8–12 KiB free. Reserving the
codec object alone did not cover all allocations deferred to its first decode.
The first input-ownership correction also treated a parser's empty state
transition as an immediate failure.

All source types now decode their first frame and initialize I2S before FIFO
allocation. A first frame that fits 4,608 bytes permits shrinking the original
8 KiB PCM allocation with `realloc`, preserving the PCM; failure retains the
original allocation. Larger first frames retain 8 KiB. AAC/TS uses two 8 KiB
FIFO blocks only when the post-initialization heap supports them, a 5 KiB reader
stack and a 16 KiB control/network reserve. Otherwise it uses one block, or
refuses startup if the reserve would be consumed. Workspace is 26.5 KiB with
two FIFO blocks or 18.5 KiB with one when compaction succeeds; MP3 stays 34.5 KiB.
This does not reserve a guarantee against all future dynamic allocations.

Radio HTTPS selects the SDK's [post-handshake static RX strategy](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/protocols/esp_http_client.html#_CPPv428HTTP_TLS_DYN_BUF_RX_STATIC):
the full receive-record block is allocated before codec/FIFO pressure and kept
until that connection closes, while TX remains dynamic. No SDK source patch or
TLS-record limit reduction was applied. Decoder calls preserve input through
buffered output and permit up to eight consecutive empty parser transitions;
persistent lack of progress fails with a diagnostic.

Build: PASS, full isolated gate; static SRAM recovery remains 33,444 bytes.
Host tests: PASS, including 30 session paths repeated three times with
ASan/UBSan, first-frame/I2S ordering, transient versus persistent no-progress,
large-PCM retention, PCM-preserving compaction, compaction failure, FIFO-budget
boundaries, partial-allocation cleanup and production HTTP/HLS fixtures.
Full image: `564e83947572933d75461b7e7813b016efc96369ca86799494e0b5e43c1c6f51`.
Matching ELF: `94e1d489101432b5f2c07ea0686024258445631f61056d684bdbeaad6a4e0152`.
Archive: `build/firmware/564e83947572933d75461b7e7813b016efc96369ca86799494e0b5e43c1c6f51/`.
Device tests: PASS for the scoped acceptance. App-only flash passed verification
with identical partitions and preserved settings. The user confirmed that
Economy Radio played normally during the requested approximately 30-second
trial, voice input worked, and listening resumed after voice ended. This is
physical user feedback; USB disconnected before the monitor captured playback,
so no matching playback heap/codec measurements are claimed for this trial.
The monitor closed and no port remains held.
Unverified: other stations, extended AAC/HLS and marginal-network playback,
repeated voice interruption/recovery, switching/exit, battery-only latency and
energy. No additional code change or flash followed this acceptance.


### CNR-9 HLS refresh correction

On the previously installed `564e8394…` image, CNR-9 decoded and played for about
eight seconds on each attempt. All three attempts then rejected the refreshed
HLS playlist; the reader still held 14,768–15,144 bytes, with zero FIFO refills
and no decoder/allocation failure. The final generic connection error hid the
playlist failure. A public-source comparison confirmed that retaining the CDN
query returns a media playlist, while dropping it returns a master playlist.

IDF 5.5.3's `esp_http_client_get_url()` omits its separate query field. The source
now retains the input URL and resolves complete `Location` targets itself, then
passes absolute destinations to IDF. Query-only and relative redirects retain
the correct path. Segment targets reuse existing playlist line scratch; header
scratch uses existing task stacks. No persistent task, timer, buffer, stack-size
increase or SDK patch was added. Master lists without `CODECS` are admitted up
to 384 kbit/s if no video is advertised; real media/codec and memory admission
checks still run. Rejections log bounded parser state without signed URLs.

FIFO policy is unchanged: MP3 24 KiB; AAC/TS 16 KiB or 8 KiB by the existing
16 KiB control/network-reserve budget. At the measured roughly 230 kbit/s CNR-9
transport rate, 16 KiB covers about 0.57 seconds; two seconds would require about
56 KiB, another 40 KiB beyond the current FIFO. Startup's roughly 30–35 KiB free
heap cannot supply that increase while keeping network/control headroom. This
fix addresses playlist refresh, not arbitrary network-jitter tolerance.

Build: PASS, complete isolated gate and verified merged/debug archive.
Host tests: PASS, including a regression that failed before the correction;
signed URL refresh across multiple windows, absolute/query-only redirects,
invalid/oversized targets, missing codec metadata, video/unsupported-codec
rejection and existing session ownership/partial-allocation checks.
A separate Mac transport check ran the production source code with a parallel
16 KiB queue against public CNR-9 for 90 seconds: 2,478,488 bytes and 13 playlist
responses, ending at the test deadline without source failure. It does not
measure device memory, decoding, audible quality or RF behavior.
Static shared SRAM remains 136,926 bytes; application image grows 336 bytes.
Full image: `be235c5f3b74748828cbf5800b18e7f08d5dc4a68ea7b4e60b72f8ebe2074f51`.
Matching ELF: `9d254402689bc9f3cb98322b1aadac02abaff8445d178fa07be691594bb4995c`.
Archive: `build/firmware/be235c5f3b74748828cbf5800b18e7f08d5dc4a68ea7b4e60b72f8ebe2074f51/`.
Device tests: application-only flash at 0x10000 verified; identical partition
table, with Wi-Fi/pairing/station storage untouched. Boot ELF matches.
Device tests: PASS for the requested CNR-9 acceptance. The user confirmed
continuous playback during the requested approximately two-minute trial, no
obvious stutter or short-play failure, working voice input and resumed listening.
Playback events were not captured in this latest monitor window; the earlier
heap figures remain measurements of the preceding image, not this trial.
The monitor is closed and the serial port is released.
Unverified: sustained playback across other stations, weak-network behavior,
repeated recovery and battery-side current. Client unchanged; no commit/push/release.

### Adaptive FIFO and compact radio interface

All supported radio formats now attempt three independent 8 KiB FIFO blocks,
falling back to two or one. Admission follows actual first-frame decoding and
I2S setup. Allocate the required 5 KiB reader stack before optional FIFO blocks;
a failed optional allocation retains the smaller FIFO. Recheck actual free heap
after allocation and trim optional blocks to retain the existing 16 KiB
network/control reserve. No additional radio task, stack or resident PCM buffer
was introduced. This budget improves resilience; it cannot guarantee allocation
success later in a session or conceal an unavailable station.

The radio module uses a centred station name, tuning marks, playback state and
a smaller next-station preview. One 200 ms vertical position animation runs
only on station changes, clipped to the existing name band. It is cancelled on
voice input, screen off, navigation and palette rebuild. There are six labels
and one clipping container, no image snapshot or continuous animation.
The animation-active flag is atomic because its completion callback and the
main task's pre-lock screen-off check run in different tasks.
More Functions now has two full-width rows: Device Settings and Radio. Manual
pause/resume was removed; voice interruption/resumption and long-press exit
remain. Re-entering Radio also starts a newly populated, previously empty list.

The C3 build uses 136,950 bytes of static shared SRAM, up 24 bytes from
`be235c5f…`; the application image grows 2,128 bytes. In the native renderer,
matched dark-radio objects use 248 bytes less of the existing fixed LVGL pool,
and More Functions uses 3,992 bytes less. The pool reservation itself is
unchanged. A single native animation temporarily uses 168 bytes; 100 changes
show no cumulative growth (16 bytes of bounded allocator slack, 32-byte limit).
These native measurements are not device heap or current measurements.

The reported theme incident was investigated without adding another clock
service. The user confirmed correct time after a cold boot and then confirmed
the device's persisted theme was **manual Dark**. Automatic scheduling does not
override a manual mode. Existing automatic-theme code already evaluates current
time rather than relying on observing the switch instant. Fresh-process native
regressions passed for day/night boot, time becoming valid after boot, and a
screen-off interval crossing a switch boundary. This does not prove the earlier
incident had the same cause. The user subsequently switched to Automatic and
confirmed correct mode and colour after a complete power-off/power-on test.
Cold boot without a connected client still has no independent time source;
network time acquisition was discussed but not implemented in this change.

Build: PASS, complete isolated ESP-IDF 5.5.3 gate and debug-archive verification.
Host tests: PASS, including reserve thresholds, partial allocation/fallback,
cleanup, model navigation and the existing protocol/session suite. Actual-font
native rendering, clipping, animation lifecycle, 100 switches and the four
theme-boot scenarios also passed.
Final full image: `ac4bc301339e5caced080bc57c77e54bd3ea4ecf9abf7d73a163b258d46341f3`.
Matching ELF: `d49c8f63223408e45cb22b0baa108ebf61dc0bcd3cf520ecce8490c1f0ef85e3`.
Archive: `build/firmware/ac4bc301339e5caced080bc57c77e54bd3ea4ecf9abf7d73a163b258d46341f3/`.
Device tests: application-only write at 0x10000 verified after checking identical
partition tables; Wi-Fi, pairing and station storage were not written.
The initial measurement image `d9cfe258…` (before the atomic-flag hardening)
observed 24,576-byte FIFOs with 32,908–56,640 bytes
free after allocation, and 8,192-byte fallbacks with 20,548–22,460 bytes free.
One fallback session reported repeated FIFO underruns; the smaller budget does
not establish uninterrupted audio. No allocation failure was captured in these
sampled starts. Logs do not establish the appearance or audible quality.
The final application's write verification and matching boot ELF prefix passed.
Device tests: PASS for the scoped final-image acceptance. The user explicitly
confirmed the new layout and station-change animation, listening during the
requested trial, voice interruption/resumed listening, and long-press exit were
normal. This user confirmation supersedes the earlier ambiguous test-speech
reply and does not establish stability across all sources or longer sessions.
Unverified: remaining stations' selected FIFO sizes, long playback, weak
networks and battery current. Client and Agent notification payloads unchanged;
conversation-identification work remains a proposal. No commit/push/release.

### Independent clock calibration, lighter canvas and live volume

The device's system clock remains the only calendar-time source. An already
valid clock is used immediately; SNTP and authenticated client status calibrate
that clock. Existing theme evaluation reacts to its corrected value on refresh
or wake, without restarting a timer or application. Duration timing remains
monotonic so calibration cannot jump a Pomodoro countdown.

The network task now starts SDK SNTP only when the clock is invalid, Wi-Fi is
connected, capture/setup is inactive and free heap is at least 20 KiB. Use one
server per 12-second attempt; rotate Aliyun, Cloudflare and pool.ntp.org with
5-second, 1-minute, 5-minute and capped 15-minute backoff. A successful clock,
disconnect or capture/setup releases SNTP. The completion semaphore and callback
are disabled; no new task, task stack, large buffer, Flash timestamp or screen
wake was added. Brief completion polling stops after success. Failed UDP
allocation/initialization is cleaned up and deferred. Offline deadlines cannot
cause a busy receive loop. The 16-byte policy state uses the existing network
stack; SDK transient UDP/DNS/storage uses ordinary shared heap.

The whole increment adds 56 bytes of static shared SRAM (137,006 total), with
unchanged 8 KiB network stack reservation and LVGL/FIFO budgets. The application
image grows 5,696 bytes. No isolated device measurement of transient SNTP heap
peak was obtained; weather overlaps the observed startup window.

The light canvas changes from #B8B29B to #D2CEBD and its panel from #AAA98F to
#C3C4AD; text and semantic colors retain their roles. Actual-font native render
fixtures, theme rebuilds, animation/popups and the four clock/boot scenarios pass.
The client no longer rejects a validated volume-only save during audio use.
Other changes remain gated. Existing persistence/revision/rollback paths are
retained; the existing playback owner applies volume at the next PCM frame,
without restarting a stream or unmuting capture output.

Build: PASS, full isolated ESP-IDF 5.5.3 gate and verified debug archive.
Host tests: PASS, including initialization/UDP failure, low heap, timeout/backoff,
server rotation, disconnect/capture cancellation, valid-client-clock takeover,
no requests after success and 100 repeated SNTP lifecycles. Volume policy tests
cover 0..100 and rejection of mixed changes; the real threaded playback suite
adds live volume transition without reconnect, with 31 paths repeated three
times under ASan/UBSan. Native rendering and static glyph coverage also pass.
Full image: `bcfcaed20de052fd9f4d6f882fd938ded00c442ff0ef5f5be341e6e0fb847caa`.
Matching ELF: `f6bdfb08d29a04eadfc617e9c47a52f4e77bc1a17064701e78f01bfcf407c506`.
Archive: `build/firmware/bcfcaed20de052fd9f4d6f882fd938ded00c442ff0ef5f5be341e6e0fb847caa/`.
Device tests: application-only write at 0x10000 verified with matching partitions;
stored Wi-Fi, pairing and stations were not written. Boot ELF prefix matches.
A preliminary serial reset retained valid time and therefore did not exercise
SNTP. Following actual USB removal and physical power-off/power-on, with the
client exited, logs captured SNTP start at 12,580 ms and valid-clock cleanup at
15,782 ms. The user confirmed correct independent time and automatic theme.
The serial monitor closed; the client was reopened and observed connected with
its existing shortcut permission. The user confirmed that a volume-only save
changed audible level immediately without interrupting playback. The user found
the #D2CEBD canvas too pale and requested an intermediate tone and stronger color.
Unverified: cold boot without internet, blocked/failing public servers on hardware,
isolated transient heap peak and long-term drift/current. The theme is revised
below. No client update, commit, push or release.

### Light-tone refinement and clearer graphic accents

Following the user's LCD feedback, the light canvas is now #C6C1AD, between the
previous #B8B29B and #D2CEBD. The panel is #B4B79C. Semantic text inks remain deep;
existing icons and vector strokes use teal #00765D, blue #0965AD, orange #B54716
and ochre #936100. Apply this separation to voice/weather icons, key marks,
tuning needle, progress and hourly charts. No layout, image asset, UI object,
draw surface, task, stack or timer was added. Dark colors remain unchanged.

Build: PASS, complete isolated gate and verified archive. Static shared SRAM
remains 137,006 bytes, zero increase from `bcfcaed2…`; application Flash grows
192 bytes. Host tests: PASS; actual-font native fixtures, popup/animation
lifecycle and four clock/theme boot scenarios also pass. Device clock and
volume code are unchanged from the preceding physically accepted image.
Full image: `04d5b470d05fc73e1818a3512ceca7f68f2dd47ffe7742e14cece9db4c1f4fb7`.
Matching ELF: `f0213d0f9a7519dc5f6aa5d42a679dfd4b99b6cd0d2084eceea7e0b1a18b24a0`.
Archive: `build/firmware/04d5b470d05fc73e1818a3512ceca7f68f2dd47ffe7742e14cece9db4c1f4fb7/`.
Device tests: final application-only write verified, partition table unchanged,
matching boot ELF prefix observed; Wi-Fi/pairing/stations/settings storage was
not written. The bounded boot check closed and released the serial port.
Physical feedback: the user found this version improved, specifically the
weather icons and red side-key accent. Most text colors still appeared similar.
Treat fine text-hue differences as unreliable on the tested display; retain
readable text contrast and use typography/layout plus chromatic icons and
strokes for hierarchy. This feedback does not isolate panel quality, backlight
or calibration as the cause. No further firmware change followed this feedback.
Unverified: color reproduction/calibration and the preceding clock/energy
limitations. No client update, commit, push or release.

### Light text hierarchy and distinct state inks

Keep the accepted #C6C1AD canvas, #B4B79C panels and graphic accents. Change
ordinary text to neutral primary #1B2529 and secondary #3F4B50, with current
temperature and ordinary settings values in primary ink. Reserve more distinct
teal #005749, orange-red #8C2C0C and blue #004E96 for voice, focus and rest/Enter;
amber is #654600. Selected settings keep neutral text, including on the darker
selection background; the existing reversed value, arrows and outline continue
to indicate editing. Align the compact notification's two neutral text inks.
No strings, font assets/sizes, geometry, objects, tasks, timers or draw surfaces
were added. Clock, radio, input and desktop code were not changed.

Host tests: PASS. The full gate's host stage includes coverage of 672 non-ASCII
characters. The native RGB565 renderer passes its actual-font layout, popup,
animation and live-theme lifecycle fixtures, plus four separate clock/theme
boot processes. All 210 matched memory fixtures retain exactly the same pool
usage and recorded peak. All 65 matched named dark frames are byte-identical;
all 82 matched named light frames reflect the new text colours. RGB565 colour
calculation gives 8.82:1 for primary text and 5.21:1 for secondary text on the
canvas, at least 4.72:1 for the selected state inks, 4.51:1 for secondary text on
a panel, 6.08:1 for selected settings and 4.89:1 for reversed editing values.
These calculations and host renders do not establish physical LCD appearance.

Build: PASS, complete isolated gate and independently verified archive. Static
shared SRAM remains 137,006 bytes, with no increase from `04d5b470…`. Application
Flash increases by 48 bytes, to 2,851,392 bytes.
Full image: `8fd3a669d046f05d63bca069a5b669ae34c315dbde6d0e8748c295d06a0898c2`.
Matching ELF: `c1657ed676a5327451c0402608470e9ba66172098fdd4693df5db396c32baa05`.
Archive: `build/firmware/8fd3a669d046f05d63bca069a5b669ae34c315dbde6d0e8748c295d06a0898c2/`.
Device tests: the application-only write at 0x10000 passed checksum verification;
the partition table matches the installed image. Wi-Fi, pairing, stations and
settings storage were not written. Bounded startup observation matched ELF
prefix `c1657ed67`, found no crash signatures and closed the serial port.
Physical feedback: the user confirmed that the light-theme text hierarchy and
key colours are noticeably clearer when reviewing voice, Pomodoro and settings
selection/editing. Keep this palette as the accepted light-theme baseline.
Unverified: objective LCD colour calibration and preceding clock/energy
limitations. No client update, commit, push or release.

### Radio exit confirmation

Long DOWN in Radio opens a direct-choice confirmation. UP cancels and DOWN
confirms stop/exit; SETTINGS has no action. Keep the selected station and
listening intent while opening and cancelling. These transitions dispatch no
audio action, so the existing stream continues without a reconnect. Only the
confirming short DOWN emits the existing radio-stop action. An entry hold,
repeated LONG or its release cannot confirm or send Enter. Ready/empty/failed
radio states use the same confirmation flow. Outside this page, Home's voice,
Enter and station-change controls are unchanged.

Reuse the former unused radio page enum slot as `V_RADIO_EXIT` and the timer
confirmation's title, state, value label and narrow key rail. The value label
uses the existing 20 px font for a bounded two-line station name. Replace the
Home page rather than layering a second UI; no fields, image/font assets,
buffers, tasks or timers are added. The player and its buffer policy are
unchanged.

Host tests: PASS for sanitizer-enabled model tests, including wake, empty
lists, cancellation, explicit stop and gesture suppression. Native RGB565
renders pass both themes, long/empty/error content, 200 open/cancel cycles,
bounded pool usage and smaller confirmation-page usage than Radio Home.
Notification overlay tests now include this page: eleven backgrounds, four
events, both themes and 2,200 restoration cycles pass. Existing layout,
animation and clock/theme boot cases also pass. Font coverage passes for 671
non-ASCII characters; no font data was changed.

Build: PASS, complete isolated gate and independently verified archive. Static
shared SRAM remains 137,006 bytes, with zero increase from `8fd3a669…`.
Application Flash decreases by 176 bytes, to 2,851,216 bytes.
Full image: `814e3a75deba47662e3b379b4f00dda53c8f0424d34dd660748808109b177a19`.
Matching ELF: `0d60fbab61bac75aa2701cb8e8fd872dc4cf562c79705307934ab823af9a58ac`.
Archive: `build/firmware/814e3a75deba47662e3b379b4f00dda53c8f0424d34dd660748808109b177a19/`.
Device tests: application-only write at 0x10000 passed checksum verification;
partition table matches the installed image. Bounded startup observation
matched ELF prefix `0d60fbab6`, with no crash signatures; the serial port was
closed. Wi-Fi, pairing, stations and settings storage were not written.
Physical feedback: the user confirmed all three requested checks: listening
continues in confirmation, UP cancels without interrupting playback, and a
separate short DOWN stops and exits.
Unverified: long-name/empty/error layout boundaries on the physical display.
No client update, commit, push or release.

## Local release review and overlapping Agent callbacks

The user selected local preparation for review, without creating a repository,
committing, pushing, tagging or publishing. The bilingual root READMEs now introduce
the product, show actual production UI, state requirements and link to focused
usage, installation, Agent and design guides. Reproducible AppKit/LVGL artwork
uses sample data without launching the installed app or contacting the device.
Community templates, manual upstream sync, desktop PR validation and the release
overview were aligned with this application. Retained firmware/font licenses are
included in redistribution notices; packaging tests exercise standalone guides,
portable checksums and missing or ambiguous payload rejection.

The approved notification policy delivers the first event immediately. Additional
callbacks for the same Agent and known session within one second are ignored,
across event kinds, without extending that window. Different Agents/sessions
remain independent; unknown identities are not grouped. Existing three-second
exact-event retry filtering remains. No new Hook, delayed candidate, metadata
lookup or firmware state was added. Seven-Agent tests cover overlap, the exact
window boundary, independent sessions and retries. The Chinese ready label now
explicitly says voice input is ready; actual LVGL rendering and glyph coverage
passed.

Build: PASS. The complete isolated ESP-IDF gate verified the merged image and
matching debug archive. Static shared SRAM remains 137,006 bytes; the application
remains 2,851,216 bytes. Merged SHA-256:
`621a965186f3c3736d92e43fc7ee6021036f7e107a4c03cb489a11ce75e612cc`.
Matching ELF SHA-256:
`366693615d73637292642a04bb554fdc7d2c8b3ccf4dc37eede27adb6dbcde96`.
Native macOS builds, tests, ad-hoc signature verification, universal ARM64/x86_64
inspection and DMG verification passed. Local review packages use version 0.7.2;
this does not select or publish a public release version.
Host tests: PASS, including protocol/Agent integration, local radio-probe streams,
production UI lifecycle/render fixtures and release packaging/checksums.
Device tests: NOT RUN for this iteration; the installed app and firmware were
not replaced during documentation review. Earlier physical acceptance records
above remain historical results.
Unverified: a real Claude options/approval overlap, current firmware text on the
physical screen, clean-system and Intel installation, and remote GitHub Actions.
The local Command Line Tools linker warned that its compatibility archive has no
x86_64 slice; the universal executable was produced, but Intel execution remains
unverified. No commits, pushes or releases were made.

## DSH path copying and Chinese publication artwork

DSH now exposes a copy-directory action after its local plugin manifest exists.
The action copies the complete plain filesystem path, gives immediate success
feedback and resets that feedback when preparing again. Finder reveal uses the
same directory and preparation guard. It does not install a plugin or add Hooks
merely by copying a path. The bilingual Agent guide documents this workflow. The desktop card now shows
three installation steps: prepare/copy, open DSH Plugins > Add plugin, then paste
and install. The action buttons sit below the instructions. The updated native
app was installed and the complete three-line layout was visually verified.

The user requested Chinese, sharp publication artwork without device-resolution
constraints or preview labels. The hero and feature gallery are rendered at
3,200 pixels wide using high-resolution native text/vector illustrations of the
current UI. The separate actual LVGL frames remain available as a development
reference. Artwork is documentation-only; firmware allocation and UI assets are
unchanged.

Build: PASS, universal macOS 0.8.1, native Agent/protocol tests, signature and DMG
verification; the full repository/firmware gate also passed. Host tests: PASS.
Desktop acceptance: the installed executable was replaced after the user
confirmed no unsaved drafts, then matched the tested executable hash. The DSH
copy button visibly reported success. The previous installed client lacked the
new session-overlap tests/logic, explaining why the reported Claude retest had
not exercised the fix. After updating, the user triggered a real Claude
options/approval interaction and confirmed receipt of exactly one alert.
Device tests: NOT RUN; no device behavior changed in this increment.
Unverified: Intel execution. Following the
ad-hoc app update, shortcut permission was not ready; no grant was requested
because the current acceptance scope is Agent alerts. No Git writes or release
publication occurred.



## Standby diagnostics layout and on-demand heap snapshot

The Connection page now keeps standby diagnostics expanded. Window metrics,
runtime heap and boot-cumulative power locks have separate sections; short
sleep intervals retain millisecond precision. Identical lock names/types are
grouped without adding their overlapping hold durations. Both I2S channels
remain in the copied raw report. Read failures preserve the last successful
snapshot with its timestamp. Read, Reset and Copy remain explicit actions.

Each device request also reads existing 8-bit heap allocator counters before
allocating the response: used/free bytes, largest contiguous free block and
per-region historical low-water marks. No task, timer, history buffer or Flash
writes were added. Static DRAM/BSS/IRAM are unchanged versus the preceding
local release candidate; the full image grows by 176 bytes. The temporary
`multi_heap_info_t` is 28 bytes on ESP32-C3, with additional transient JSON costs.
Reset clears window sleep totals, not boot lock history or heap low-water marks.

Validation: full repository gate PASS; universal Mac build, native tests and
image/signature checks PASS. The actual device was updated at `0x10000`, with
compatible partitions and untouched NVS. Wi-Fi and pairing reconnected. The
40-second startup window matched ELF `d966a7c44f5b9215…` and contained no detected
faults. Actual UI Read, Reset and Copy succeeded: the device reported 127.0 KiB
allocated, 122.8 KiB available, 84.0 KiB largest block and 83.2 KiB historical
minimum at that read. The USB host was connected, so this is neither a
battery-only sleep test nor a current measurement. Long-term standby draw and
heap behavior under radio load were not remeasured in this iteration.

Verified local archive:
`build/firmware/5b929206a83487090664fc81e684a86b0b211b480557a9bfd042b4738c336cb2/`.
Full SHA-256: `5b929206a83487090664fc81e684a86b0b211b480557a9bfd042b4738c336cb2`.
Matching ELF SHA-256: `d966a7c44f5b9215182ce1c6f33eb948f8cb21a91a0cbfeaa7bafae624ac20c5`.
Local review package: `.work/release-prep/review-package-power/`.
No commit, push, tag or public release was made.


## Complete diagnostic fields and concise model-facing report

Kept the accepted diagnostics layout. Added the reported total successful sleep
call time, explicit connected/disconnected USB state and lock-report completeness
below the existing table. The heap heading now identifies dynamic heap memory.
The copied report uses original millisecond/byte values, booleans, null for
unavailable data and individual raw lock records. It omits transport request IDs,
all explanatory prose and recalculable display values. Only collection time
and raw data remain; matching code and DESIGN retain the counter/heap scope
and sleep-overhead definitions.

Build: PASS (universal Mac app, full repository/firmware gate, signature and DMG).
Host tests: PASS, including exact report values, absent versus empty lock data,
disconnected USB and malformed/truncated lock preservation.
Device tests: PASS for actual LAN reads, bottom field rendering and the Copy action
in the updated installed client. The existing sleep window was not reset.
Unverified: Intel execution and long-term power draw. No firmware source changed,
no device flash was performed, and no device resident allocation was added.
The device retains the previously tested image `5b929206…336cb2`.

The unchanged firmware was rebuilt and verified by the complete gate:
full SHA-256 `cadb4cbfc648c76afa05b8846f95a51ea2b309406fb6cf9b03dbd773e423acac`;
matching ELF SHA-256 `e4c4601a8cfb757f304158a6316d4764e6aa7d1c27644e5921c75f2abdce32d6`.
Archive: `build/firmware/cadb4cbfc648c76afa05b8846f95a51ea2b309406fb6cf9b03dbd773e423acac/`.
Updated local review package: `.work/release-prep/review-package-power-report-data-only/`.
No commit, push, tag or publication was made.

## Wi-Fi, BLE and computer ownership

The connection architecture is defined in [Connections](connections.md).
The device remembers eight computer identities and permits one active owner.
Wi-Fi and BLE share each computer's credential; at most two BLE links are kept
for the owner and a temporary takeover connection. Healthy routes remain selected,
and failover considers the same computer before another paired computer.

Protocol 3 controls are bound to both the device boot nonce and ownership lease.
The controller rejects stale claims, old configuration writes and previous-boot
messages. The desktop validates complete revision headers, rejects downgrades,
and confirms takeover from an authoritative ownership announcement. Wireless
switches can be confirmed on the surviving route if the original acknowledgement
is lost. Other old-route operations are discarded. Physical-code pairing can
recover one computer's credential without changing other records.

The BLE adapter preserves restored subscriptions, rejects reused connection
handles, bounds fragment assembly and HCI retries, and joins the host task before
shutdown. With ESP-IDF 5.5.3, runtime-block pools are disabled while dynamic
contexts remain enabled to permit safe teardown and reclamation. BLE startup
follows resident Wi-Fi/UI allocation. The network worker uses eventfd for local
wakeups, including BLE-only operation; no polling task is added.

## Computer-relayed radio and diagnostic history

Radio decoding runs in a separate, on-demand FFmpeg subprocess on the active
computer. The packaged universal decoder includes corresponding LGPL sources
and its build recipe. The device receives independent 50 ms, 24 kHz mono IMA
ADPCM blocks through a credit-controlled, segmented 15,704 / 23,556-byte FIFO.
The resident voice PCM reservation and 16 KiB control/network margin are retained.
Voice preemption stops the relay and releases disposable radio allocations.

The device diagnostic ring occupies 200 bytes. Against the relay-only reference
build, initialized DRAM and BSS increased by 304 bytes in total. No diagnostic
sampling task, periodic timer or Flash logging is added. The desktop retains
at most 500 typed records / 512 KiB, catches up offline records, deduplicates
retries and exports only approved fields. Raw URLs, credentials, network
addresses, decoder stderr and conversation text are excluded.

Connection and voice readiness are independent UI states. Wi-Fi and BLE each
show their status, while permission/driver errors remain in the voice section.
Device statistics and diagnostic history share one destination. Agent installation
is read from native configuration; DSH detection checks registered bundles and
installed files rather than relying on prior events. Opening a non-owner client
window does not unpark its BLE connection.

## Validation — 2026-11-12

- Build: PASS — complete repository gate, merged-image verification, universal
  macOS packaging, signatures and DMG checks. The desktop also built from a clean
  source archive with a newly downloaded and compiled decoder.
- Host tests: PASS — ASan/UBSan ownership, controller, BLE lifecycle and relay
  tests; 100,000 deterministic ownership operations; actual loopback socket and
  desktop dispatcher tests; C-to-Swift audio/diagnostic fixtures; native arm64 and
  Rosetta x86_64 protocol/Agent suites. Cases include NVS rollback, credential
  repair, stale controls, restart, ownership announcement before reply and ACK
  loss during a wireless switch.
- Device tests: PASS within one-device/one-Mac coverage — compatible application
  update, boot-image verification, retained settings, repeated Wi-Fi/BLE switching,
  live statistics over either standalone route, BLE shutdown/restart and correct
  switch confirmation. A BLE drop during Wi-Fi startup recovered automatically;
  seamless handoff is not guaranteed. The final ownership fixes leave DRAM
  data/BSS and IRAM sizes unchanged.
- Unverified: two physical computers switching concurrently, multiple real
  networks, prolonged coexistence, physical speech/Enter and radio playback during
  handoff, Intel hardware, clean-system permissions and battery current. Model and
  dispatcher tests do not establish these end-to-end results.

Reference device test image SHA-256:
`bb8a49d1f44aef3f9e54639087eb507db203748126af32d242de05037a6a236d`.
Matching ELF SHA-256:
`8c0e14c9432d88a40f26e0ec45e6a9d68323bf979b9de5d8687d2f18c5649586`.
