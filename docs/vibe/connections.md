**English** · [简体中文](connections.zh_CN.md)

# Computers and wireless links

## Identity and ownership

The device remembers up to eight computers. A computer has a random persistent
32-character client ID, its own 64-character credential, and a display name.
Names, IP addresses and Bluetooth addresses are never identity keys. Both links
use the same client ID and credential. The client stores these in private files
under its Application Support directory, without calling Keychain.

The old single credential becomes an unnamed legacy record. The first client
that proves knowledge of that credential attaches its persistent ID and name;
the firmware saves the migrated record before accepting it. Existing Wi-Fi,
preferences and stations use their original keys and layouts.

One computer owns voice, Enter, settings and Agent notifications at a time.
Other paired computers can authenticate, read the owner status and explicitly
request **Switch to this computer**. Discovery never preempts a healthy owner.
The screen displays the owner's name; duplicate names do not merge computers.

## Pairing

On the device, open **Device settings → Pair computer** to show the six-digit
code. Discover it in the desktop client over the LAN or BLE and enter that code.
An initial unpaired device also shows the code on Home. Pairing through either
link authorizes the other link for that same client; no second application code
is required. System Bluetooth permission and OS-managed Bluetooth bonding are
separate from this application pairing.

Bluetooth-only operation does not require Wi-Fi provisioning. Wi-Fi provisioning
continues to use the existing phone portal. Turning both radios off requires
using the device settings to re-enable one. Clearing pairings clears all computer
credentials; it does not erase saved Wi-Fi or station lists.

If a computer loses its local credential but retains its client ID, entering the
current physical pairing code replaces only that computer's credential. Other
computers and the registry capacity are preserved; a failed save changes nothing.
An inactive paired computer can request takeover even while its BLE connection
is parked: **Switch to this computer** reconnects before claiming ownership.

## Routing and reconnection

Wi-Fi discovery retains UDP port 33333 on the device. The computer binds an
available OS-assigned port and broadcasts on each active IPv4 LAN. Replies use
the actual source address and port. Authenticated `pc.status` messages report
readiness and device time synchronization on both available links every 10 s.
The device expires a missing link after three intervals.

The device chooses the initial available route, preferring Wi-Fi when both are
known, then keeps a healthy route. If that route fails, the other authenticated
route to the **same computer** is considered before another paired computer.
Reconnection does not automatically promote Wi-Fi over a working BLE route.
An explicit ownership change or endpoint replacement increments a boot-scoped
lease. Control requests must carry both the current boot nonce and lease.
A delayed takeover from a replaced owner is rejected; retrying the current
owner's successful claim is idempotent. Delayed requests, queued
Enter events and audio from an old owner cannot reach the new owner. The client
rejects older leases and retired boot nonces and releases held shortcut keys
when the owner, route or device boot changes. Mid-recording failover may end the
utterance; seamless lossless handoff is not promised.

Protocol 3 HELLO and event counters must be complete unsigned 32-bit integers;
malformed headers cannot reset the client's fence, and a live protocol 3 session
cannot downgrade to protocol 2. The first valid ownership announcement confirms
a pending takeover, even if it precedes the request-specific reply. Update the
firmware and client together for the protocol 3 boot-bound control format.
The client retains initial compatibility with released protocol 2 firmware.

## BLE

The device is a NimBLE peripheral and never scans. It allows two simultaneous
connections: the owner and a temporary status/takeover connection. Advertising
continues while one slot remains free. The second connection expires after 30 s
unless it becomes the owner; inactive clients release it proactively and
reconnect when the user requests takeover. Remembering eight computers does not
mean keeping eight BLE links alive.
Opening the client window refreshes readiness without unparking that connection;
manual refresh and explicit takeover can resume discovery.

The desktop stops BLE discovery and reconnects when a fresh device status says
Bluetooth is disabled. Enabling it again over Wi-Fi resumes discovery; manual
refresh can also find a device re-enabled locally without Wi-Fi. Enabling a
radio never cancels another computer's ownership or unparks an inactive client.
The BLE scheduler remains independent of LAN socket creation, so a LAN socket
failure cannot stop Bluetooth heartbeats and request deadlines.

A static random BLE identity is generated once and persisted in NVS. It remains
stable across reboots and computer switching, and is separate from the original
firmware's public BLE address/bond cache. It does not replace the factory device
ID read over GATT. Never generate a new BLE identity at every boot to conceal
bond-storage defects.
After verifying the factory device ID over GATT, the Mac caches the system's
peripheral UUID and tries retrieving it before scanning again. This cache never
replaces device identity verification or authorization.

The service is `6F6C6F56-6962-436F-8D11-0B4DE2C07400`:

| Suffix | Purpose |
| --- | --- |
| `01` | Read the stable device ID before choosing a stored credential |
| `02` | Encrypted GATT writes with response, carrying control packets |
| `03` | Notifications carrying replies, events and ADPCM audio |

Both directions fragment the existing typed packet behind a six-byte
little-endian header: message number, offset and whole packet length. MTU is
negotiated, with an application value cap of 244 B. Reassembly is bounded to
1,537 B, rejects discontinuous fragments and expires after 2 s. Incoming device
reassembly storage exists only while receiving a command. Voice retains the
existing independent 100 ms IMA ADPCM blocks and 3,200 B resident PCM reservation.

## Memory and power rules

The host registry and route state are fixed bounded structures, rather than a
task per computer. JSON queue entries carry a source route, not a full inline
packet buffer. BLE pools allocate their configured bounded capacity at stack
startup and release it at shutdown; they cannot borrow the radio's 16 KiB
network/control reserve.
Prepare the BLE adapter's locks before starting the network worker, but defer
the Bluetooth host and controller to that worker, after the UI and Wi-Fi's
resident allocations. Do not interleave Wi-Fi lifetime allocations with BLE
pools: stopping Bluetooth cannot coalesce holes separated by live Wi-Fi blocks.
The existing network worker waits on UDP and one internal eventfd. BLE commands
can wake it even with all IP interfaces down. This adds no task or periodic
polling; it also avoids allocating a loopback UDP packet for each local wake.
The BLE state mutex is never held across NimBLE calls. A separate packet mutex
serializes fragments and stack teardown; host callbacks never acquire it. Restored TX subscriptions can precede the SDK's
CONNECT callback and must survive it. Events for other characteristics must not
change this subscription. Transient HCI queue pressure retries the same fragment
within a bounded deadline, without adding a resident packet FIFO. Disabling
Bluetooth stops and joins the host task before deinitializing the stack. It does
not irreversibly release controller memory that would prevent re-enabling BLE
without rebooting. Turning off Wi-Fi stops its radio and connection attempts.
ESP-IDF 5.5.3's combination of runtime-block pools and dynamic-context teardown
has ATT/GATT teardown ordering and pool cleanup problems. Explicitly disable
`BT_NIMBLE_MEMPOOL_RUNTIME_ALLOC` while retaining `BT_NIMBLE_STATIC_TO_DYNAMIC`.
Do not patch the installed SDK, disable assertions to conceal leaks, or retain
Bluetooth pools permanently to avoid teardown. Revalidate stop/start and memory
reclamation after an SDK upgrade. The `vibe_ble_switch`
light-sleep lock is held only during stack transitions and released on return,
never during normal standby.

Controller and low-rate host code run from Flash instead of occupying optional
IRAM. This preserves contiguous heap without shrinking the voice reservation
or radio safety margin. Flash erase can delay BLE processing, so NVS saves,
bond persistence, voice throughput and reconnects require hardware checks.
Do not enable chip-specific Flash auto-suspend without identifying compatible
hardware. HCI queues are bounded for two links, rather than retaining the
default general-purpose scanner's large queues.

Idle BLE requests peripheral latency; only voice routed over BLE requests
low-latency parameters.
The central may negotiate different parameters. Advertising is 0.8–1 s, with no
device-side scanning or application heartbeat per BLE connection event. Modem
sleep and automatic light sleep remain enabled. This board uses the main
crystal for BLE sleep timing; adding an external 32 kHz crystal is not a software
setting. Sleep statistics and heap figures do not measure battery current.
BLE control requests retry after 3 s to avoid premature duplicates on an idle
link. After changing wireless switches, a fresh authenticated HELLO with the
correct owner, lease and requested switch values can confirm success if the
old route's acknowledgement was lost. A stale state or different owner cannot.
If the old link closes first, retain only the pending wireless-switch intent,
without retrying it before ownership is re-established; expire it after 12 s if
no fresh status confirms it. Discard other old-route operations immediately.

## Computer-relayed radio

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

## Validation boundary

Host tests cover legacy migration and persistence failure, duplicate names,
route stickiness, two-computer takeover, same-host failover, HCI handle reuse,
stale requests, bounded fragmentation, malformed input and serial wraparound.
They include 100,000 deterministic ownership operations and desktop packet-
dispatcher tests using real loopback sockets, covering old settings, old boot
events, ownership announcements before replies and downgrade rejection.
They cannot establish antenna performance, actual BLE throughput, two real
computers' OS behavior, radio/audio quality or battery current. Record those
against the exact tested firmware in the implementation record.
