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

## Connect and change computers

1. Install matching firmware and desktop builds; allow Bluetooth/local-network access.
2. Click **Connect** in **Connection & voice**. The last four ID digits distinguish units.
3. The device wakes and displays the requesting computer name. Press the upper key
   to accept, or the lower/settings key to cancel.
4. Authorized computers reconnect automatically. Wi-Fi and BLE can remain attached
   together. Business traffic prefers Wi-Fi and falls back to BLE after approximately
   three seconds without a Wi-Fi heartbeat, without another confirmation or transport picker.

Another computer can request a connection directly. The device asks to switch;
only acceptance and successful credential persistence revoke the previous authorization.
Denial, cancellation and expiry preserve the old binding. A revoked desktop pauses
reconnection instead of competing. **Disconnect** keeps authorization and pauses
reconnection. **Unpair** revokes the computer's application authorization while
retaining saved Wi-Fi and OS-level BLE encryption bonds. A retained bond cannot
authorize commands or bypass a fresh physical confirmation. Erasing BLE keys on
only one side causes a stale-pairing failure on macOS and is not part of this action.
Selecting another device asks before discarding unsaved settings.

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
without replaying duplicates. BLE supports the same audio and controls. Discovery replies contain device identity, occupancy and
capabilities without changing the control destination. Broadcasts never contain
credentials. `connection.request` carries client UUID, request UUID, printable ASCII
computer alias and a SHA-256 commitment to a candidate credential. Repeating the
same request over either link neither extends its deadline nor replaces another
computer's pending request.

The device waits forty-five seconds for a button decision, followed by twenty
seconds for credential commitment after acceptance. Client, request and digest
must match the approval. Persistence precedes replacing the old authorization.
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
Business messages use one primary link; audio sequences and critical event IDs
provide loss/duplicate checks.

Authenticated `pc.status` beacons carry `primaryTransport` (`wifi` or `bluetooth`).
The desktop reports its receive-side choice immediately when it changes, so
one-way Wi-Fi loss cannot leave the device sending into a failed return path.
The device honors a BLE preference only while that link is fresh, and otherwise
uses fresh Wi-Fi. Older peers without the field retain automatic selection.

## Agent notices and voice wake

For a known Agent session, the desktop derives a stable 36-character UUID from
the Agent, session and notice kind. Different turns of the same kind therefore
share the device's notice ID and do not ring again. Missing or unknown sessions
retain a random ID. When a physical key dismisses a visible notice, the device
sends `agent.dismissed` with the decimal FNV-1a 64-bit hash of that UUID over
both links. The desktop uses it to mute later notices of that Agent, session
and kind; other kinds remain available. Older peers ignore unknown events.

On a dark-screen wake with voice not ready, the device sends a rate-limited
`pc.ping`. An authenticated desktop with a credential immediately sends a
unicast beacon to refresh readiness. If capture starts while voice remains
unavailable, the device reports `voice.ignored` with reason `not_ready`.
The desktop records that diagnostic without changing the connection state.


## Acceptance

Host tests cover identity merging, dual attachment, fallback, denial, expiry,
busy state, wrong commitments, revoked authorization, framing boundaries and
session generations. Hardware acceptance still needs multiple computers/devices,
confirmation rendering/buttons, reconnection on each transport, voice/Enter,
Agent notifications, settings, switch latency, coexistence memory and sleep behavior.

For an already-authorized device, close the desktop and run
`<installed-app>/Contents/MacOS/VibeCompanion --ble-device-test`.
This helper uses a 90-second acceptance window and the real engine without a LAN socket, reads
settings and the radio list, submits the unchanged brightness value, sends one
Agent test notification and checks a minute of BLE availability. It never pairs
or bypasses device approval. Restart the desktop after the test.
