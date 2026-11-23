**English** · [简体中文](voice-dual-link.zh_CN.md)

# Redundant voice uplink

This development branch duplicates microphone audio over authenticated Wi-Fi
and BLE attachments to the same computer. It does not combine their bandwidth.
One timely copy is enough; two missing copies remain a loss. Both radios share
the ESP32-C3 and 2.4 GHz airtime, so physical coexistence and memory acceptance
are required before a release. The single-primary checkpoint remains available.

## Negotiation and wire format

An authenticated `pc.status` advertises `voiceV2: 1`; the device records this
capability in RAM. Its `device.hello` also advertises `voiceV2: 1`. A recording
snapshots the negotiated mode and owner generation at start. Without the
capability, firmware sends the existing type-1, 807-byte single-primary format.
The desktop continues to accept that format from older firmware.

V2 `voice.start` and `voice.end` retain the mirrored, three-copy critical-event
mechanism and shared increasing `eid`. They add `voiceVersion: 2` and a
`voiceSession` containing sixteen lowercase hexadecimal digits for a random,
nonzero 64-bit recording nonce. End also carries `frames`, the exclusive final
block count. Both paths send identical audio bytes:

| Offset | Length | Content |
| --- | --- | --- |
| 0 | 1 | Packet type `6` |
| 1 | 8 | Recording nonce, little endian |
| 9 | 4 | Block sequence, little endian, starting at zero |
| 13 | 804 | Independent IMA ADPCM block, 1600 mono samples at 16 kHz |

The complete packet is 817 bytes, including the type, on UDP and inside BLE
framing. The existing 180-second recording limit remains. V2 rejects sequences
at or above 2000 and terminal counts above 2000; this bounds malformed input
and leaves margin beyond the recording limit. The sequence does not wrap during
a recording. Only the authorized endpoint can supply business audio. This is
the existing trusted-LAN security model, not additional UDP authentication.

## Independent sending and bounded storage

The capture worker encodes each 100 ms block once. With a fresh Wi-Fi attachment,
it submits UDP immediately using a local packet buffer. After successful kernel
submission, it offers the same body to one pending BLE slot and wakes the
existing network worker. Replacing a pending copy is counted. No extra task,
heap-backed audio queue or larger NimBLE pool is introduced.

The worker removes a pending copy before sending, so BLE cannot hold its slot
mutex across radio work. One static in-flight slot and one pending slot use
about 1.7 KB of device RAM, plus the slot mutex and counters. They carry owner
and BLE connection generations; stale ownership, reused BLE handles, and copies
older than 200 ms are discarded. A failed UDP submission or BLE-only operation
uses the existing bounded direct BLE path. UDP submission success is not proof
of delivery; the redundant copy is still needed for one-way Wi-Fi loss.

Type 6 receives the same voice admission policy as type 1: preserve six total
mbuf blocks and four blocks in the consumed pool, with a 30 ms send budget.
One leg failing never erases authorization or requires a new pairing. Normal
radio playback and configuration protocols retain their existing behavior.

## Desktop merge and lifecycle

The desktop validates the header and ADPCM block before admitting it to the
current recording. A compressed reorder window holds at most eight blocks.
The first copy of a sequence wins; later copies do not decode or refresh the
voice watchdog. Playback begins after a 120 ms prefill and advances at 100 ms
per block, using a 20 ms timer only while recording. This adds a deliberate
120 ms recovery budget to audio latency. Missing leading blocks are skipped at
the first deadline; a block repaired during prefill can still play.

After playout begins, a missing block can spend up to 200 ms of additional
waiting across the entire recording. The output naturally pauses while waiting;
a first copy arriving in that interval preserves the speech instead of deleting
it. Total prefill plus rescue waiting is capped at 320 ms, excluding capture,
transport and system output time. The budget never resets at every lost block.
Once exhausted, missing blocks become silence and late copies are discarded. Large sequence gaps and scheduler delays resynchronize the
bounded window instead of replaying seconds of stale speech. Terminal counts
stop synthetic silence and size the final drain grace, capped at one second.
No audio packet can initiate a recording. Event ordering scoped to device ID
and boot prevents delayed starts from reopening a released session; session
binding prevents an old end from stopping a later recording.

In V2 the control primary retains silence/disconnect-based stability rules, but
voice loss no longer commands audio route changes: audio already uses both
attachments. Legacy recording retains its previous failover policy. The
4.5-second fresh-audio watchdog, permission-loss handling, recording limit and
immediate release on disconnect/sleep/quit remain. The merge timer and buffers
are disposed with the recording; idle operation gains no periodic audio work.
Up to eight distinct Enter actions received during normal audio drain wait until
the shortcut is released. Mirrored copies remain deduplicated. Abnormal stop
discards pending Enter actions, preventing delayed input after sleep or loss.

## Validation and diagnostics

Host checks exercise the production C sender under blocked BLE, copy replacement,
expiry, revoked generations and single-link operation. C-generated wire vectors
are parsed and decoded sample-for-sample by Swift. Timeline tests cover duplicate
suppression, complementary/shared loss, reordering, leading loss, stale sessions,
terminal counts, scheduler stalls and bounded storage. Existing host and firmware
gates and desktop radio/permission/sleep regressions remain required.

Logs distinguish capture count, UDP/BLE submissions, overwritten/expired copies,
received copies by leg, admitted blocks, duplicates, late packets, repaired holes,
missed playout blocks, rescued deadlines, added delay and resynchronizations.
Live copy sending performs no periodic console I/O; cumulative device counters
are reported after capture. Routine memory/stack reports defer during audio.
Desktop audio-device availability refreshes asynchronously at most once per
second; audio packets never enumerate CoreAudio devices, and audio-only UI
updates are capped at five per second. Sleep invalidates pending probes. Active voice uses a scoped latency-critical
activity that allows system and display sleep, released before audio disposal
on every stop. First-copy arrival-gap and late-first-copy counters distinguish
packets that missed playout from copies that never arrived; late duplicates do
not establish RF loss. The desktop seen-set is bounded by the 2000-frame limit.
A repaired hole is not a measured
RF loss rate. No audio, credentials or user speech is logged.

Hardware acceptance compares identical long recordings with Wi-Fi only, BLE
only and both, including live loss of either attachment and full loss/recovery.
Check input/Enter, memory low-water marks, ATT pressure, coexistence, trailing
words, computer sleep and radio interruption/resumption. Simulator success is
not proof of physical radio reliability or a completed hardware soak.
