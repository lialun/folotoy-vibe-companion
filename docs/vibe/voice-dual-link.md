**English** · [简体中文](voice-dual-link.zh_CN.md)

# Voice pipeline

The device captures speech once. A single sender delivers the encoded block over
Wi-Fi, BLE, or both authorized attachments to the same computer. The computer
orders and decodes the first useful copy into BlackHole. Redundancy does not
combine bandwidth or guarantee recovery when both paths fail. ESP32-C3 shares
its radio between Wi-Fi and BLE.

## Responsibilities and ownership

| Module | Responsibility | Execution owner |
| --- | --- | --- |
| `main/vibe/voice_policy.h` / `macos/app/VoicePolicy.swift` | Local memory, retry, playout, and input budgets | Compile-time policy |
| `main/vibe/voice_wire.h` / `macos/app/VoiceProtocol.swift` | Wire layouts, limits, capabilities, and control identities | Protocol boundary |
| `main/vibe/model.c` | Allocate a monotonically increasing recording request | Application task |
| `main/vibe/voice_capture.c` | Capture, encode, and enqueue; finish only its own request | Audio task |
| `main/vibe/voice_tx.c` | Bounded storage, independent carrier attempts, and repair policy | Pure state machine under the uplink mutex |
| `main/vibe/voice_uplink.c` | Queue admission and one send opportunity per carrier per service cycle | Capture producer; network consumer |
| `main/vibe/voice_power.c` / `main/vibe/wifi_power.h` | Recording performance reservation and overlapping voice/radio Wi-Fi power policy | Network task; pure policy |
| `main/vibe/network.c` | Authenticated endpoints and wire-format adapter | Network task |
| `main/vibe/ble_sender.inc` | Resumable notification submission without retaining payload | Network task; existing BLE send mutex |
| `macos/app/BLEFrame.swift` | Independent, bounded control and voice reassembly | Desktop Bluetooth queue |
| `macos/app/VoiceSession.swift` | One input/output lifetime, repair, drain, and cancellation | Desktop network queue |
| `macos/app/VoiceMerge.swift` | Deduplication, order, and known-gap playout | Pure state machine |
| `macos/app/VoiceRepair.swift` | One pending repair reservation, submission outcomes, and mirrored receipts | Pure state machine |
| `macos/app/BLEWriteBuffer.swift` | Complete packet ownership, fragment cursor, and expiry outcomes | Pure state machine |
| `macos/app/BLEWriteFlight.swift` | ATT in-flight/backoff ownership and stale retry isolation | Pure state machine |
| `macos/app/BLEWritePacing.swift` | Whole-packet control/radio budgets, with priority for voice control | Pure state machine |
| `macos/app/PCMContinuity.swift` | Internal output interruption counters, excluding startup/tail | Pure state machine under the output lock |
| `macos/app/AudioSink.swift` | Fixed PCM ring and CoreAudio buffer completion | Network producer; CoreAudio consumer |
| `macos/app/InputCommands.swift` | Explicit, scoped, expiring Enter commands | Desktop network queue |

The connection Engine forwards authenticated events and supplies transport/output
ports. It does not implement a second voice state machine. VoiceSession depends
on `VoicePCMOutput` and `VoiceShortcut`, so deterministic tests do not open an
audio device or inject real keys. Its shortcut interface has no Enter method.

The capture task never calls UDP or NimBLE. It briefly locks the ring to copy
one encoded block. The sender copies an attempt out before calling either
stack; network I/O never holds that ring lock. Capture snapshots only its
request, gain, and capabilities, rather than copying unrelated weather/UI data
onto its task stack. Both legacy and modern capture stop if the computer-owner
generation changes. Send tickets include recording,
owner, sequence, and slot creation time, so an old completion cannot modify a
replaced recording or overwritten slot.

All business UDP writes belong to the network task, including legacy voice.
Cross-task wakeups use a separate loopback socket with a nonwaiting writer lock.
They never write the business socket. BLE callbacks only enqueue inbound data;
they do not call lwIP. Voice and input lifecycle messages retain their producer's
computer-owner generation. A queued old event is discarded after handover and
cannot be retargeted to the newly selected computer.

The network task handles at most four queued control messages per turn. It
services voice between inbound packets, before a control batch, and between
outbound messages and critical mirrors once receiver readiness has been acknowledged. Older peers
retain control-before-audio ordering, so scheduling priority cannot send their
first audio ahead of start. This bounds a control burst without adding another socket
writer. Revocation waits until the queued control replies drain, even when the
batch spans several turns.

## Recording lifecycle

An UP press creates a new `voice_request`. Capture snapshots it, a random nonzero
64-bit recording nonce, and the authorized computer generation. Release stops
that request. If a new press arrives before the old worker finishes, cleanup of
the old request cannot clear the new model state. Failure messages carry the
request ID for the same reason.

An authenticated `pc.status` advertises `voiceV2: 1`, `voiceRepair: 1`, and
`voiceReady: 1`. `voiceInterleave: 1` independently declares that BLE control
fragments can interleave with voice. Discovery beacons carry the same capability
map as status, from one desktop declaration. V2 `voice.start`
contains `voiceVersion`, `voiceSession`, `voiceRequest`, retained repair count,
active link mask, and `ready`. If readiness is negotiated, capture waits for an
authenticated `voice.ready` for that nonce and owner before producing audio.
It retries the same start every 250 ms, for at most 1.5 seconds, and observes
release/replacement/cancellation while waiting. A duplicate start only repeats
the desktop acknowledgement; it never reopens the shortcut or audio device.
Older computers retain the short legacy startup delay.

Output and shortcut startup belong to the admitted recording lifetime. A failure
or refusal at either step uses the same terminal cleanup: release keys, stop
output, close the nonce, cancel device capture, and report one unsuccessful
result. A later start copy cannot reopen it. CoreAudio enqueue failures are
checked; if output stops asynchronously, the next voice tick cancels input.

`voice.end` contains the nonce, request, exclusive final block count, device
monotonic timestamp, and a stop reason:

| `stop` | Meaning |
| --- | --- |
| 0 | User release |
| 1 | Link/readiness lost |
| 2 | Recording limit |
| 3 | Audio source failure |
| 4 | Replaced by another request |
| 5 | Uplink allocation failure |
| 6 | Receiver readiness timeout |
| 7 | Computer cancellation |

Lifecycle events are mirrored three times with one shared event ID. The desktop
rejects closed nonces, old sessions, duplicates, invalid counts, and invalid
reasons. An end received before its start closes that nonce. It cannot trigger
a delayed automatic recording.

The desktop holds the shortcut until the known final blocks and CoreAudio
buffers drain. A two-second tail deadline bounds a failed drain. The 181-second
receiver capture ceiling applies only while capturing; a valid device end
transfers time-budget ownership to draining. Crossing the capture ceiling
cannot discard a late final block still within that tail deadline. Disconnect,
permission loss, a 4.5-second fresh-audio timeout, replacement, and sleep cancel
the session and pending input. Computer cancellation also sends `voice.cancel`
for the current nonce; an updated device observes it at a capture block boundary.
Sleep releases keys immediately and tears down output on its owning queue.
Timing activity exists only during a recording and allows system/display sleep.

The device uplink owns one performance lifetime through capture and its bounded
repair tail (600 ms for old peers, up to three seconds with feedback). The network worker applies an optional CPU maximum-frequency
reservation outside the ring mutex, once per transition. This also prevents
automatic light sleep on the pinned ESP-IDF power manager; idle, cancellation
and owner replacement release it. Maintenance runs even when legacy control
ordering suppresses audio submission, so queued control cannot pin this lifetime.
An unavailable reservation leaves voice functional at the normal clock policy.

The same network worker is the only Wi-Fi power-save writer. It disables modem
sleep while either the voice uplink or active radio needs traffic. Radio finish
cannot put an unfinished voice tail to sleep, and voice finish cannot put radio
to sleep. A small pure transition policy coalesces idle work and reapplies the
request after AP-mode changes or driver restart. Capture and radio workers no
longer set Wi-Fi power directly. This policy does not force a PHY data rate,
raise transmitter power beyond SDK limits, or create a permanent awake task.
The audio worker publishes radio activity through an atomic hint and a
transition wakeup; the network worker does not acquire the UI/model mutex to
decide power policy on each audio send.

The current radio profile uses eight dynamic Wi-Fi TX buffers, disables TX
aggregation while retaining RX aggregation, and sets IP precedence 4 on the
business UDP socket. HTTP uses separate sockets. This profile adds bounded
driver headroom and prioritizes interactive traffic; it does not reserve
airtime, pin an access point, or guarantee delivery on a congested/shared radio.


## Wire format and bounded transmission

The internal encoded body is always the V2 form. The network adapter converts
it for older peers; there is no separate legacy capture or direct-send path.

| Offset in a V2 datagram | Bytes | Content |
| --- | --- | --- |
| 0 | 1 | Type `6` |
| 1 | 8 | Recording nonce, little endian |
| 9 | 4 | Sequence starting at zero, little endian |
| 13 | 804 | Independent 100 ms IMA ADPCM block, 1600 mono samples at 16 kHz |

The packet is 817 bytes on UDP and inside BLE framing. The legacy adapter emits
807 bytes: type `1`, wrapping eight-bit sequence, marker `0x80`, and the same
ADPCM block. New audio still rejects sequences/counts above the existing
2000-frame bound; capture retains its 180-second limit. The authorized endpoint
is required on both routes; this does not add UDP encryption.

At startup, one reserved media arena is sized to 6–30 encoded blocks while
leaving at least 22 KiB of normal internal heap available at reservation time. Actual capacity is
bounded by the largest contiguous allocation and is reported at startup. Voice
and radio hold exclusive generation-scoped leases on the same arena; stale
releases cannot revoke a new lease. Capture PCM is also reused for radio decode.
Radio has no separate frame/PCM allocation while stopped. Its small catalog and
control metadata remain. An interrupted radio session disables ingress and
returns its arena before capture; after capture it waits for the voice tail
before borrowing the arena again. The display uses one ten-line DMA stripe;
wireless pools are unchanged. The audio worker uses an 8 KiB stack after removing
large snapshots; device capture/radio measurements retain more than 3 KiB
of unused stack in the instrumented build. This startup heap reserve is not
a guarantee against later allocations.

The one shared ring replaces separate live-copy and repair queues. Each leg has
its own pending state and attempt count. One carrier's refusal does not clear
the other's pending data or block capture. Both adapters still execute on one
network worker, so this is not wall-clock isolation: UDP `MSG_DONTWAIT` alone
does not bound waiting for the TCP/IP worker. Each carrier samples current
monotonic time under the ring lock immediately before selecting a frame.

Each voice service cycle submits at most one block per carrier, with service
cycles spaced at least 20 ms apart. Attempts making no progress back off
independently per carrier by 20, 40, then at most 80 ms, up to eight consecutive
no-progress attempts. A partial result means newly submitted bytes, not merely
an old prefix still waiting. Actual progress resets the no-progress streak and
backoff; the outer service cycle still bounds rate. Charging normal fragment
credit flow as repeated failure can make four fragments take 140 ms despite
20 ms credits, slower than capture. No progress never resets that budget. Local stack admission no longer shortens
the sibling copy's opportunity. Older peers use six blocks and a 600 ms capture-age
limit. Peers negotiating `voiceFeedback` use the actual arena capacity times
100 ms, up to three seconds. Receipts cancel both pending copies immediately;
retry frequency and no-progress limits remain independent and bounded. A new
frame or repair does not bypass an existing carrier backoff. Explicit repair
can use retained blocks within the negotiated history limit, with at
most two repair requests per block. An adapter reports no submission, a resumable prefix, or complete submission.
A partial packet acquires a lease on that carrier until completion or its
existing expiry, eviction, no-progress attempt limit, or recording replacement. Another
frame cannot reset the peer's single voice assembly while that lease remains
valid; no-credit retries preserve it. The other carrier remains independent.
The scheduler owns only frame identity; the BLE adapter alone owns byte offsets.
Without a partial packet, the earliest absolute expiry is served first, with
sequence order breaking ties, so continuous capture cannot starve a repair
approaching eviction. Storage and
retry age never grow during congestion. The voice lease ends on receiver completion, cancellation, owner change, or
bounded tail expiry. Its reserved arena remains available to radio or the next
recording; no active voice polling or performance assertion persists while idle.

When `voiceInterleave` is negotiated, BLE voice sends never wait for a lock or pool.
A partially submitted frame resumes at its first unsubmitted byte with the same
message ID. Its cursor holds no payload and is invalidated by a different nonce,
sequence, connection generation, or the 600 ms retention limit. The sender
policy still decides whether an attempt is eligible; a resumable cursor does
not extend that frame's lifetime. Control messages may
interleave; the updated receiver keeps separate voice and control assemblies,
each bounded to 1537 bytes and two seconds. Older V2/legacy peers retain the
bounded whole-packet sender because their receiver has only one assembly.

Whole-frame headroom is checked
before the first practical full-MTU fragment; this is admission forecasting,
not an atomic controller-credit reservation. Existing ATT headroom, reserve
blocks, and the two 12-block NimBLE pools remain unchanged. The whole-frame
forecast uses aggregate capacity; each actual fragment still preserves the
selected pool's reserve. It does not require all fragments to use one pool. Successful queue or
UDP submission is not proof of application delivery.

Updated desktops put a recording-scoped, increasing `repairID` on each logical
repair request. Its mirrored packets carry the same ID. Each retained block
rejects an already accepted or older ID before charging its two-request budget,
even when the first repair already completed. A later logical retry has a new
ID and retains its allowance. This prevents delayed cross-carrier duplicates
from manufacturing extra repair traffic or consuming the next retry. Missing
IDs retain the bounded behavior for older desktops; full tagged deduplication
requires both updated endpoints. ID zero, fractions, booleans, and values above
32 bits are rejected when the field is present.

The desktop reserves at most one logical repair at a time. Queue admission is
not a submitted attempt. The BLE queue owns complete packets and emits exactly
one result on complete local submission, expiry, cancellation, or reset.
Peers supporting write commands use one uniform credit-based write kind for
control and repair, preserving complete-packet order without per-fragment ATT
round trips. Other peers retain acknowledged writes. CoreBluetooth fragment
credits alone do not bound whole-request production: `BLEWritePacing` spaces
ordinary controls by 200 ms and radio packets by 50 ms. Ready, cancel, and repair
messages bypass unstarted background packets in voice FIFO order, never an
actual partial assembly. A single on-demand timer follows the current queue
head and is cancelled on teardown; no permanent pacing poll remains while idle.
A 300 ms local queue deadline bounds repair admission. Completion is local
submission, not remote application acknowledgement. An
independent deadline expires queued work even while another ATT write is in
flight, retaining only the actual outstanding fragment for its callback.
Each fragment remains within a single ATT value; long-write support is not
assumed. If all mirrored copies are definitely unsent, the reservation is
released without spending an attempt. A submitted copy commits the attempt;
a disconnect during the final write is conservatively uncertain. Retry age
starts at completion, with the same 350 ms budget on every route. A recording
starting with both links may later rely entirely on BLE; its start mask must
not prematurely consume both attempts while a slower recovery is returning.
`BLEWriteFlight` distinguishes an actual ATT fragment from a
retry delay. Removing a backoff head invalidates its timer without releasing a
newer in-flight write. Old recording/request completions cannot affect a new
reservation. The adapter owns callbacks; the queue and repair policy stay pure.

## BLE streaming profile

The network worker combines live voice (including its tail) and radio intent. One
coalesced host-queue event passes that intent to the existing parameter
coordinator; capture never runs a GAP procedure. Streaming requests a 15 ms
connection interval. When combined streaming intent ends, or during reconnect backoff, it requests the original 15–30 ms
idle range after a one-second grace, covering quick voice/radio transitions. No new task or permanent profile poll is added.

The pure parameter policy preserves initial negotiation settle time, serializes
accepted procedures, applies the newest intent after an older completion, and
bounds busy/error/timeout retries. Unsolicited events cannot acknowledge a
queued retry. A central-selected valid fallback is accepted without repeated
reconnection; 15 ms is a request, not a peer guarantee. Apple documents the
[15 ms exception and possible 30 ms fallback](https://developer.apple.com/library/archive/qa/qa1931/index.html).

On 2026-11-25 the on-demand profile passed four BLE cases, including injected
loss, partial submission, and control bursts: 2141 frames, no missing frames,
no internal PCM gaps, and no pool pressure. UART observations verified nine
successful changes between a 15 ms streaming interval and a 30 ms idle interval.
These results do not establish Wi-Fi performance, real input-method recognition,
or power consumption on this or other hosts.

## Playout and explicit input

The desktop keeps at most 32 compressed blocks. Every modern recording
uses a 600 ms initial reservoir. A larger sender history does not automatically
increase this startup latency or guarantee recovery after equally long stalls. Route changes do not shrink it. This trades 350 ms of additional
startup latency on Wi-Fi/dual voice, or 100 ms on BLE-only voice, for continuity
across a slower useful carrier; the PCM ring is unchanged. It is a bounded
jitter reserve, not protection against arbitrary outages. A known missing sequence can
use the speech already queued ahead of it plus another 120 ms for repair. Repair requires an observed later block,
a final count, or a bounded initial-block probe. Negotiated feedback is coalesced at 200 ms; older peers receive no ACK stream.

A clock tick never advances beyond the known produced frontier. When arrivals
stop, it waits instead of declaring successive future blocks lost and later
trying to replay/reanchor speech. A persistent delay step therefore does not
make every subsequent frame permanently late. Host scheduling delays preserve
queued speech instead of skipping it to catch a wall clock. After the initial
prefill, consecutive blocks move into PCM as capacity becomes available;
CoreAudio owns the playback rate. A second software clock must not hold that
prefill in compressed storage while the actual PCM consumer runs empty.
An empty produced frontier does not reapply startup prefill. Buffer overflow is
bounded and counted, including losses detected on admission.

PCM uses a fixed 16000-sample ring. Each handoff is bounded by free ring capacity;
backpressure retains the remaining compressed blocks. An overflow cancels explicitly instead of
silently clearing the preceding utterance. `PCMContinuity` counts empty output only between two pieces of speech; startup
and a drained tail do not inflate its interruption metrics. End diagnostics
report interruption count/duration separately from missing blocks. CoreAudio completion counts returned
buffers, not just data copied into them. These counts establish output delivery,
not what the input method recognized.

Updated firmware advertises `inputCommands: 1`. A valid DOWN gesture creates one
`key.action` with `inputVersion: 1`, monotonic command ID, recording request,
press time, and release time. Times originate in the button callback, rather
than when a delayed application queue processes it. Input queue overflow clears
held state without reusing old command IDs.

The input controller validates scope and device-time ordering independently of
voice. One-way discovery observations are monotonic device-time watermarks,
not clock synchronization. An event more than one second behind the newest
authenticated device observation is rejected; reordered observations cannot
move this watermark backward. Idle Enter does not depend on a converted Mac
deadline, so independent clock drift cannot permanently disable it. Without a
newer device observation, this ordering check does not establish an absolute
end-to-end transport age. Cancellation still fences gestures started before
its estimated device-time boundary; a matching voice-end timestamp refreshes
the boundary before completion processing.

At most four explicit intents can wait for their own recording to drain, and
expire one second after local receipt. Only a nonempty,
normal, lossless, completely drained session can release them. Failed sessions,
new recordings, disconnect, and sleep destroy them. No voice start, stop,
timeout, recovery, or output callback can manufacture an Enter. Every actual
injection logs its command identity. Old firmware retains immediate idle Enter;
its unscoped events are never deferred. Full scoped-input guarantees require
both updated endpoints.

## Extension and review rules

The pipeline has one recording owner on each side and one device network writer.
It uses concrete state machines and small ports, without a transport plugin bus
or a generic workflow framework. `Engine` remains the connection/composition
owner; its unrelated radio/settings functions are not part of VoiceSession.

| Future change | Primary edit boundary | Required regression |
| --- | --- | --- |
| Retry, prefill, timeout, or memory tuning | `voice_policy.h`, `VoicePolicy.swift` | Retention/latency bounds, starvation, output drain, and expired input |
| BLE/UDP submission changes | Network adapter and BLE sender/reassembler | One stalled carrier, partial admission, control interleaving, and owner replacement |
| Another voice trigger, such as tap-to-toggle | Device model/input mapping | Request identity, replacement, sleep, and explicit stop; capture/transport contract stays intact |
| Different output or shortcut integration | `VoicePCMOutput` / `VoiceShortcut` adapters | Partial startup, asynchronous output stop, teardown, and zero uncommanded Enter |
| A codec or wire-format change | Codec and protocol boundary, with explicit negotiation | C/Swift golden vectors, old-peer compatibility, bounds, and independent block decoding |

Wire bounds and fields are separate from local tuning policy. Do not infer one
capability from another, change packet sizes as a latency adjustment, add direct
network calls to capture, or recreate a pending-Enter counter in Engine. Keep
all VoiceSession and InputCommands entry points on the desktop network queue;
CoreAudio callbacks touch only the sink's locked PCM/health state. Capture and
network hold the model, peer, and uplink-state locks only for snapshots/state
changes, never across audio or network I/O. The BLE submission mutex separately
serializes a bounded stack operation. Output adapters must report health through `running`
and drain only when queued and in-flight samples are both zero.

The architecture provides isolated places to optimize; it does not prove RF
performance, input-method recognition, or freedom from every future bug. Real
carrier stress, physical-button, sleep, and input-method acceptance remain
separate from deterministic tests.

## Validation and diagnostics

The original accelerated on-device baseline on 2026-11-25 passed 34 of 50
cases. Preserve that result as a baseline, not acceptance of the revised code.
Complete frame counts alone do not establish continuous playback: acceptance
also compares source/received/played identities, ordering, PCM append/read hashes,
internal output gaps, and zero uncommanded Enter injections.

The revised on-demand BLE parameter policy passed four BLE application-transport
cases (2141 blocks), including injected first-copy loss, partial submission and
control bursts, with no missing blocks or internal PCM gaps. A 450 ms persistent
transit-delay replay fails the previous output budget and passes the uniform
600 ms prefill. A separate long-recording regression ensures the capture limit
cannot truncate an already draining final block. These deterministic checks and
the bounded BLE run do not establish real input-method recognition.

The 2026-11-25 capture-and-tail performance implementation passed 50 cases at a
strong-signal AP: the 43-case mixed-route matrix, three independent Wi-Fi
application-transport cases, and four independent BLE application-transport
cases. All 15154 normal-recording blocks matched capture, receipt and playback
counts and hashes, with no internal PCM gaps or uncommanded Enter injections.
The intentional outage is separate from that block total: audio timed out and
cancelled, did not restart automatically, and a new recovery recording passed.
A read-only SDK lock report confirmed release before subsequent recordings.

On 2026-11-26 a deterministic host trace exposed a scheduling defect: while a
Wi-Fi submission yielded for 120 ms, capture queued a new block; the shared
service timestamp then expired that block's BLE copy. The per-carrier clock
fix passes that previously failing trace and 24 success/failure timing traces
from 0 through 700 ms, including copy/retention boundaries. The 50 passing
hardware cases above predate this fix.

Subsequent weak-AP testing of the same production core passed ten audio cases.
Three alternating 90-second pairs and one 150-second pair compared BLE-only
media with dual media, keeping both physical radios connected. All 4252 BLE-only
blocks and 4243 dual blocks were received and played without missing blocks or
internal output gaps; the dual cases recorded 2852 Wi-Fi resource refusals.
A new recording after an intentional dual-media outage also passed. Source and
PCM hashes matched; keyboard injection was mocked and Enter count was zero.
Read-only timing around actual voice submissions measured maxima of 3.826 ms
for Wi-Fi and 17.528 ms for BLE, with no call exceeding 20 ms. These figures
exclude other worker operations and do not prove a general latency bound.

The initial test launch did not establish both authenticated control links
within 90 seconds and never entered recording. Preserve that setup failure
separately from the ten audio cases. The Bluetooth permission switch was already
enabled and was not changed. After closing the test application and resetting
the device without writing Flash, the next launch completed the matrix; the
initial readiness delay's cause remains unconfirmed.

Single-Wi-Fi stability remains an acceptance blocker. Before the clock fix, an
otherwise identical performance profile selecting the weak AP (initially -79 dBm) failed all three
independent Wi-Fi cases: two audio timeouts and one tail timeout. Diagnostic
packets were also lost, so their source totals are unavailable; do not invent
complete capture counts for these failures. Earlier experiments reproduced
driver resource refusals despite available internal heap, and a weak-signal
43-case matrix passed 29 cases and failed 14. Disabling TX aggregation or
reserving CPU performance alone did not resolve the issue. CPU measurements
found 160 MHz already active at capture entry; a forced PHY-rate experiment
worsened results and was rejected. AP/radio conditions and diagnostic fixtures
differed across runs: these counts are not a controlled before/after improvement
percentage or evidence that signal strength is the only cause.

Normal automatic startup also selected a -90 dBm AP after association timeouts;
a later restart of the same normal image selected a -53 dBm AP on the same
network. A private increase from three to six association attempts still fell
back to a weak AP and was rejected. Association and AP selection therefore
remain part of the investigation; extra retry delay is not a verified fix.

Automated device runs use the real microphone, radios and audio output, but mock
keyboard injection. Independent application routes and media-path masks do not
turn physical radios off. Physical radio shutdown, unmodified sleep/power
behavior, and real input-method recognition remain separate acceptance checks.
Temporary benchmark AP selection, USB wake locks and physical-key isolation
must not be delivered in normal firmware. The 50 passing cases do not override
the weak-Wi-Fi failures or establish release readiness.

Run `./macos/test-voice.sh` for deterministic tests of the production Swift
components. On macOS this also runs in `./tools/validate.sh --static`. The
universal desktop package runs the full native tests and C/Swift wire vectors.
The complete repository gate additionally builds and verifies the firmware.

Tests cover both directions of a stalled carrier, bounded retry/retention,
concurrent capture during a blocked send, partial BLE admission and resumption,
interleaved control, connection/nonce/cursor expiry, owner changes (including legacy capture), partial startup/adapter failures,
stale completions,
readiness timeout, capture replacement, persistent transit delay, host stalls,
leading/interior/final repair, actual output drain, and expired/mismatched input.
Voice-only lifecycle traces assert zero Enter injections. Automatic tests do
not establish real RF performance, input-method recognition, or perceived audio.

Optional `voice.stats` is separate from the small terminal event. Its numeric
counters cover the capture interval, so late tail submissions can occur after
the snapshot. They do not contain speech or credentials. BLE controller counters
observe actual HCI completed-packet events separately from host processing;
`NOTIFY_TX` is not a controller completion. HCI bytes include headers and other
connection traffic, not just application voice payload. The read-only observers
are bound to the pinned ESP-IDF 5.5.3 transport; host tests verify ownership,
completion versus submission, delayed host dispatch, and bounded trace storage.


### Isolated desktop bench

`python3 tools/voice_bench.py` builds a separate `Vibe Voice Bench.app` for the
current host architecture under `build/macos-voice-bench/<source-digest>/`.
It compiles the production voice engine with test observers and a simulated
keyboard port. It does not install or modify the normal application, register
login items, install agent hooks, or request Accessibility access. A build runs
the native self-tests without starting networking. The normal build omits the
bench observers.

The bundle identity is `io.github.vibecompanion.voicebench`. Plans and results
stay outside the signed application. The builder reuses a verified signed
bundle when source, compiler, SDK, target, and bundled resources are unchanged;
editing a JSON plan does not trigger compilation or signing. The bench may
need Bluetooth/local-network permission on first use. A real code update can
still require renewed system permission with development signing; this tool
does not bypass macOS authorization or promise permanent grants.

Launch the built application with `--plan <absolute-json-path>` and optionally
`--output <absolute-results-path>`. The window does not connect until **Start**.
Close the normal application before starting; the bench refuses to run while
it is present. Only the selected device, existing computer identity, and
shortcut are copied into the bench's own preference domain. The existing
device credential is read from the account's pairing store, never included in
the bundle or result. Opening the window does not change the device or normal
application preferences. Do not run another device-control client concurrently.

The runner requires separately authorized fixture firmware that implements
authenticated `voice.fixture` commands and source-hash diagnostics. Normal
firmware does not implement these commands; a command timeout is a setup
failure, not an audio result. Restore normal firmware after hardware testing.
The bench does not flash firmware automatically.

Example plans are in `macos/voice-bench/plans/`. Validate one without opening
Bluetooth or requesting permissions:

```bash
"<built-app>/Contents/MacOS/VoiceBench" --check-plan macos/voice-bench/plans/smoke.json
```

`transport` selects which desktop adapters are enabled. Within a stage, media
mask 1 uses Wi-Fi, 2 uses BLE, and 3 mirrors both; masks do not disconnect the
physical radios. Mask 0 must declare an expected outage. Plans reject unknown
fields, unavailable carriers, unordered changes, duplicate stage names, and
runs exceeding 45 minutes. Results distinguish complete audio, expected
cancellation, and setup errors, and include block/PCM hashes and mock Enter
counts. These do not establish real input-method recognition or physical
radio shutdown behavior.


### Command-line bench with existing access

`python3 tools/voice_bench.py build-backend` builds `VoiceWireBench` and a
checksum manifest under `build/macos-voice-bench-cli/<source-digest>/`. It uses
the same production engine, external plans, CoreAudio output, simulated keys,
and authorized fixture protocol as the graphical bench. It refuses to start
while the normal app or graphical bench is running.

```bash
"<backend-directory>/VoiceWireBench" --permission-check
"<backend-directory>/VoiceWireBench" /absolute/plan.json /absolute/results.json
```

The permission check reads Bluetooth authorization without connecting. BLE
plans require an existing grant in the invoking environment; otherwise the
runner exits with status 77 without requesting a new Bluetooth grant. Local
network access is still governed by macOS. The runner verifies its build
manifest before networking, reads existing device credentials, and supplies
connection inputs through a volatile defaults domain. It does not replace the
normal application, refresh its Accessibility identity, or write its settings.

Plans, results and the monotonic command sequence file must remain outside
the frozen backend directory. Results include source/build identity. Ctrl-C
requests cleanup; system sleep cancels the test instead of automatically
resuming it on wake. A hard deadline bounds the plan even if progress stops.
This is a local acceptance tool, not a firmware flasher or a way to bypass
system permissions. `--self-test` runs host tests without opening Bluetooth.

### Receiver feedback and media handoff

`voiceFeedback: 1` is negotiated through the existing capabilities and recording
start. Type 7 feedback is an explicitly encoded 54-byte message: type/version
(with a terminal flag), the existing binary pairing credential, recording nonce,
feedback ID, retire-before sequence, and a 32-bit receipt mask. It preserves
credential, endpoint, owner-generation and active-recording checks; feedback
cannot claim a session or enter the UI/input command queue. Future, duplicate,
old-owner and regressing feedback is rejected before mutating the ring.

Receipt bits describe validated blocks still owned by the receiver. Retire-before
means encoded retransmission is no longer useful, including consumed or expired
positions; it never asserts that missing speech was received. Confirmed/retired
slots cancel both carrier copies and partial-frame scheduling leases. Late send
completions and repairs cannot resurrect them. Capture does not wait for feedback.
The BLE queue replaces unstarted feedback with the newest snapshot while
preserving an actual fragment assembly.

Receiver completion is separate from ordinary receipt/retirement. The terminal
flag is sent only after output drains and shortcut release; only then may the
device return a normal voice lease early and resume radio. Cancellation and
bounded expiry still release resources when completion feedback is lost. Tail
performance and BLE streaming intent are combined with radio by the network
worker, so audio-worker cleanup cannot downgrade a still-live repair tail.

### Shared-media acceptance on 2026-11-26

Local tests used the production voice engine, real capture/radio/CoreAudio, and
a mock keyboard. Six radio-to-voice-to-radio cases passed. The final guarded
startup selected 20 slots (16,800 bytes, two seconds), with an 8 KiB audio
worker stack. Its instrumented capture/radio run retained about 3 KiB of stack.
The allocator rechecks free heap after allocation to handle concurrent SDK
allocations; the startup reserve is not a permanent runtime heap floor.

| Scenario | Result |
| --- | --- |
| BLE, including repair, radio handoff, control bursts and expected outage/recovery | 7/7 PASS |
| Dual/individual media paths, complementary loss, path changes, 150-second recording and recovery | 11/11 PASS |
| Normal-AP Wi-Fi-only, repair, controls and radio | 4/4 PASS; 889 complete blocks |
| Final allocator/BLE radio handoff check | PASS; 159 complete blocks |
| Same weak AP, approximately -78 dBm at startup | 8/9 PASS; both repeated BLE-versus-dual pairs and the 90-second dual recording had no loss or internal PCM gap |
| Weak AP without a BLE data path | Audio timeout with 74 receiver-reported missing blocks; the next fixture command timed out |

Across the recorded cases, 31/33 strict assertions passed, plus one setup timeout.
The weak-AP complementary-loss case recovered all 314 blocks in order with matching
source/received/played and PCM hashes, but had one 320-sample (20 ms) internal
PCM gap. It remains a failed continuity case; the threshold was not relaxed.
This combines real weak Wi-Fi with intentional BLE first-copy loss, so it does
not establish continuous delivery when both initial copies are unavailable.

The weak-AP selection existed only in a separate test image using RAM-only
Wi-Fi configuration. Normal firmware restores automatic AP selection. The final
normal image and desktop application were installed without erasing NVS or
pairing data. Test-only commands, button isolation and the temporary tone server
are absent from the normal image/runtime. Real input-method recognition,
subjective sound and physical radio shutdown are separate checks; battery
measurement was deferred by the user. These results are not release approval.
