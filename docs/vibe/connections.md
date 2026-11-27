**English** · [简体中文](connections.zh_CN.md)

# Device connections

The desktop discovers devices over LAN broadcasts or nearby BLE advertisements.
Sightings of the same device merge into one row. BLE carries voice, button events,
settings, radio playback, radio lists, power diagnostics and Agent notifications.
The computer fetches and decodes Internet radio, then sends audio over either
link. Radio requires the desktop app and computer Internet access; device Wi-Fi
is optional. The computer can also forward a compact weather/air cache over
either link. Direct device weather downloads remain an optional fallback when
Wi-Fi and sufficient memory are available.

## First connection

The welcome page introduces Bluetooth and Wi-Fi. Its three physical keys select
Bluetooth, Wi-Fi or Later. BLE advertising starts at boot without a transport
selection or Wi-Fi configuration. The Bluetooth guide shows desktop instructions
and the device suffix. Only an explicit Wi-Fi action opens the phone hotspot.
Successful Wi-Fi setup continues waiting for computer approval, rather than
claiming pairing is complete. Later persists; previously paired devices also
skip the welcome page after updates. Device settings retain separate computer
connection and Wi-Fi entries. Cancellation and expiry preserve saved networks,
the previous authorization and guide progress. An entire dark-screen wake
gesture is excluded from confirmation decisions.

Phone setup temporarily stops the BLE host/controller before starting the hotspot
and HTTP server. Exiting, saving or rolling back setup restores BLE automatically;
OS bonds, trusted-computer credentials and the fixed media reserve are retained.
A failed restore is retried by the existing network worker. Normal operation still
supports simultaneous Wi-Fi and BLE.

Submitting Wi-Fi credentials dismisses the phone keyboard and replaces the form
with a visible connection indicator before awaiting the server response. Reloading
an active connection restores that waiting view. Failure returns to the editable
form with the error retained; success shows confirmation before closing the page.

The page has a build-time gzip representation in flash, negotiated through
Accept-Encoding, with the original HTML as fallback. No compressor or extra page
buffer is allocated at runtime. Phone setup bounds its HTTP server to two LRU
sessions and a 4 KiB task stack.
Status replies copy only their own fields instead of the entire device status.
This leaves more heap for TCP traffic while retaining the media reserve. Startup
checks Wi-Fi setup, HTTP allocation and every route; a failure closes partial
resources and allows retry instead of advertising an unusable portal. Logs report
response errors, remaining heap, the largest block and HTTP stack headroom.

## Connect and change computers

1. Install matching firmware and desktop builds; allow Bluetooth/local-network access.
2. Click **Connect** in **Connection & voice**. The last four ID digits distinguish units.
3. The device wakes and displays the requesting computer name. Press the upper key
   to accept, or the lower key to cancel. The settings key has no action on this page.
4. Authorized computers reconnect automatically. Wi-Fi and BLE can remain attached
   together. A healthy primary link stays selected, including BLE after Wi-Fi
   returns. Sustained failure can use the other authenticated attachment without
   another confirmation.

The device remembers up to four individually approved computers, with one active
controller at a time. After each computer's first approval, taking the device
between home and work can reconnect it automatically. Another computer cannot
automatically take over while the active computer's Wi-Fi or BLE session remains
valid. A waiting desktop shows that the device is in use and reconnects once it
becomes available. It releases its standby BLE connection and waits at least
fifteen seconds before probing again, leaving a connection slot for handover.
If both computers are nearby, the first authenticated idle-session claim wins;
there is no location-based preference.

Clicking **Connect** can still request an immediate switch. Device confirmation
and successful persistence precede changing the active controller; other trusted
computers keep their credentials. Denial, cancellation and expiry preserve the
current session. **Disconnect** keeps authorization and pauses reconnection on
that desktop. Desktop **Unpair** removes only that computer; the device's
**Unpair** menu clears all trusted computers and explicitly names that scope in
its confirmation. Both retain saved Wi-Fi and
OS-level BLE encryption bonds. A removed credential pauses reconnection and
requires fresh physical approval. A retained bond cannot
authorize commands or bypass a fresh physical confirmation. Erasing BLE keys on
only one side causes a stale-pairing failure on macOS and is not part of this action.
Selecting another device asks before discarding unsaved settings.

Firmware and desktop must both support trusted-computer standby. An older
desktop may pause after a handover instead of waiting automatically. The previous
single saved credential migrates on its first authenticated reconnection; computers
whose credentials had already been replaced by old firmware need approval once.
The registry never silently evicts an approved computer when full.

An explicit disconnect of the sole BLE attachment frees its session immediately.
Silent loss retains the existing lease of three negotiated heartbeat intervals
(up to ninety seconds), so brief interference does not trigger a takeover.
Refreshing discovery does not request a takeover; click the device's **Connect**
action to request one deliberately.

## Confirmation protocol and boundaries

The desktop distinguishes an outgoing request from a device-acknowledged
pending confirmation. Only the matching pending reply enables the physical
confirmation instruction. Repeated replies do not extend its timeout. BLE
transport failures back off by 3, 6, 12, 24, 48 and at most 60 seconds; fresh
advertisements cannot bypass that interval. Explicit device selection or valid
business traffic clears the backoff. This limits retry storms without bypassing
macOS connection restrictions. Automatic attachment is driven by fresh
connectable BLE advertisements. Cached device identity alone does not start
repeated connections to a radio that is no longer advertising; manual retry
remains explicit.
If macOS explicitly reports that the peer removed pairing information, automatic
BLE retries stop. The desktop offers Bluetooth settings and an explicit retry
after the stale system record is forgotten; a healthy Wi-Fi session is retained.

LAN control, discovery and radio audio use UDP 33333. Radio audio carries a
random session and bounded sequence window; receipt masks repair missing blocks
without replaying duplicates. BLE supports the same audio and controls. Anonymous
discovery reports device identity, occupancy and capabilities without changing
the control destination. An addressed, authenticated `pc.status` discovery beacon
can claim an idle session. Broadcasts never contain
credentials. `connection.request` carries client UUID, request UUID, printable ASCII
computer alias and a SHA-256 commitment to a candidate credential. Repeating the
same request over either link neither extends its deadline nor replaces another
computer's pending request.

An authenticated discovery reply separates `trusted` (a remembered credential)
from `authorized` (this computer currently owns the session). A trusted standby
desktop retains its credential and retries without requesting confirmation.
These fields are evaluated only when `authChecked` is true; public BLE metadata
does not describe a particular computer's trust. Standby business commands are
rejected. A new owner clears old endpoints, readiness and computer quota data,
and advances the command generation; device preferences and weather stay intact.

The device waits forty-five seconds for a button decision, followed by twenty
seconds for credential commitment after acceptance. Client, request and digest
must match the approval. Persistence precedes switching the active controller.
Legacy numeric pairing cannot bypass physical approval. Recording returns busy;
the desktop can cancel. The displayed computer name is not a trusted identity.

BLE service UUID is `71DF1000-7B51-4A1D-A9B3-5A7669626532`; metadata is `1001`,
encrypted writes are `1002`, and notifications are `1003`, with matching suffixes.
The `Vibe-<12-hex-ID>` advertisement shares its device ID with LAN discovery.
BLE uses LE Secure Connections Just Works encrypted bonding without numeric input.
It does not provide authenticated MITM protection; device-button approval and the
persisted credential authorize application control. LAN UDP has no transport
encryption and is intended for trusted local networks.

Two BLE connection slots allow another computer to request a handover, without
authorizing two simultaneous controllers. Each connection has its own assembler.
Its seven-byte header contains `0x56`, message ID, start/end flags, little-endian
length and offset. Assembly is capped at 1537 bytes and two seconds. LAN and BLE
share application authorization; attaching a link does not change the session
generation. A computer handover invalidates queued commands from the old generation.
Legacy audio uses one primary link. Negotiated V2 voice duplicates both paths
and merges on the computer.
Critical voice/button events mirror the same
event ID over both authorized links; the desktop deduplicates before acting.
Audio sequences provide loss/duplicate checks.

Authenticated `pc.status` beacons carry `primaryTransport` (`wifi` or `bluetooth`).
The desktop reports its receive-side choice immediately when it changes, so
one-way Wi-Fi loss cannot leave the device sending into a failed return path.
The device honors a BLE preference only while that link is fresh, and otherwise
uses fresh Wi-Fi. Older peers without the field retain automatic selection.


## Transport stability and congestion

Audio-only switching below applies to legacy recordings. V2 already uses both
paths and retains control-silence/disconnect failover.

A recovered heartbeat does not preempt a working link. With a backup that has
received authenticated traffic within three seconds, the desktop changes the
primary only after six seconds of current-link silence, an explicit disconnect,
or three seconds without recording audio. A full five-second audio window with
at least twenty sequence positions and 70% missing is also severe failure.
Audio-only failover has a fifteen-second
hold; a disconnected/unresponsive primary can bypass it. A new recording and a
newly selected link get an audio-start grace. The existing 4.5-second no-audio
release remains, so two failed links cannot leave shortcut keys held. Minor
jitter and low signal alone do not switch routes. Both peers use the six-second
link window; a healthy BLE session has no automatic Wi-Fi switchback.

Device notifications check the projected mbuf chain cost against both the total
free blocks and the specific pool used by that chain. Voice preserves four pool
blocks and six total blocks after allocation. Under pressure, worker tasks wait
within a thirty-millisecond voice budget or fifty-millisecond control budget;
callbacks never wait. No extra audio queue or larger buffer pool is introduced.
ATT congestion retries the same desktop fragment at 100, 250, 500 and 1000 ms;
new writes cannot bypass the wait. A success resets backoff, while disconnect or
service replacement invalidates old retries. Resource/queue exhaustion is not
reported as a pairing failure. Logs retain error domain/code, per-link voice
packet counts, stop reasons, pool availability, free heap and largest free block.

## Computer sleep

System sleep immediately blocks new device work and releases held shortcut keys.
BLE scanning, connections, LAN sockets, voice/radio audio and timing activity are
then stopped on their owning queue. The notification handler does not wait for
that queue, audio drivers or subprocess startup. Transport shutdown precedes
audio disposal. Queued timers and old permission callbacks cannot restart work.

The app uses `NSWorkspace.didWakeNotification` for system wake, independently of
displays. Screen-only sleep/wake does not change the connection, and background
maintenance does not resume it. Opening the app or explicitly connecting can
recover a missed wake notification. Repeated wakes do not create extra Bluetooth
managers. Authorization, settings drafts and manual disconnect intent survive
sleep and automatic permission relaunch. Voice never restarts by itself; radio
reconnects only if the device still requests playback.

Quota polling cancels its owned subprocess without holding a lock across process
startup. A poll interrupted by sleep becomes eligible again on wake while the
source is recently active. Successful polls retain the five-minute interval,
genuine failures retain backoff, and cached observations keep their original age.

## Acceptance

Host tests cover identity merging, dual attachment, fallback, denial, expiry,
busy state, wrong commitments, revoked authorization, framing boundaries,
trusted standby, repeated automatic handovers, registry persistence failures,
legacy migration, restart decoding and session generations. Hardware acceptance still needs multiple computers/devices,
confirmation rendering/buttons, reconnection on each transport, voice/Enter,
Agent notifications, settings, switch latency, coexistence memory and sleep behavior.

For an already-authorized device, close the desktop and run
`<installed-app>/Contents/MacOS/VibeCompanion --ble-device-test`.
This helper uses a 90-second acceptance window and the real engine without a LAN socket, reads
settings and the radio list, submits the unchanged brightness value, sends one
Agent test notification and checks a minute of BLE availability. It never pairs
or bypasses device approval. Restart the desktop after the test.
