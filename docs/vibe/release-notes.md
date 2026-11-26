**English** · [简体中文](release-notes.zh_CN.md)

# Vibe Companion v1.0.1

## What changed

**A major voice-stability upgrade for steadier input across network conditions.**

This release reworks voice transport, buffering, repair and continuous playback. Wi-Fi and Bluetooth complement each other, while either link alone gains stronger bounded recovery. The aim is fewer interruptions, stalls and missing audio during brief jitter or send congestion, and more usable everyday voice interaction.

- **Better dual-link cooperation:** retry each carrier independently; the computer merges, deduplicates and acknowledges received audio so a slow carrier stops resending confirmed blocks.
- **Stronger single-link recovery:** expand bounded history and improve missing-block repair and playback continuity. Capacity follows available startup memory; the tested device reserves two seconds. Larger history does not increase the normal startup prebuffer.
- **Voice-first memory:** recording and radio share the large audio workspace. Radio yields it before recording and resumes after voice drains.
- **Usable, explicit Enter:** fix independent clock divergence persistently rejecting Enter. Retain explicit button commands, deduplication and cancellation fences. Voice stop, timeout or recovery cannot manufacture an Enter.
- **Clear recording ownership:** unify start, end, cancellation and drain; isolate old recordings, callbacks and repairs. Sleep or full disconnection never resumes recording automatically.

## Install and upgrade

Update **both the desktop app and firmware to 1.0.1** for complete receipt feedback, workspace handoff and input protection. Older peers retain their compatible protocol behavior.

Universal Apple Silicon/Intel DMG and ZIP packages are included. Follow `INSTALL.txt`. macOS may require refreshing the app's existing Accessibility permission after updating. Keep the computer awake and the app running during use.

Flash the merged `FoloToy-AI-Passport-full.bin` from `0x0`; this may reset settings and pairing. Developers retaining settings should follow the repository firmware-layout guide for a compatible application-partition upgrade. Verify `SHA256SUMS.txt` after downloading.

Packages use free ad-hoc signing and are not Apple notarized. Agent alerts, quotas, weather, Pomodoro and radio remain available.

## Validation and known limits

The complete local gate, firmware-image verification and native Mac tests passed. Recorded hardware automation passed 31 of 33 audio cases, covering individual carriers, dual delivery, repair, path changes, long recordings and radio handoff; one additional setup attempt timed out. These tests use real capture, wireless transport and CoreAudio with simulated keyboard output. The user reports improved voice behavior and working Enter; real Enter/newline was also confirmed separately.

The following boundaries remain disclosed:

- Very weak Wi-Fi alone can still time out. A weak-Wi-Fi test with injected first-copy loss recovered all audio but had one 20 ms playback gap.
- The radios share airtime and underlying scheduling can still interact. Dual delivery is not guaranteed to outperform one carrier in every environment, nor to eliminate all loss.
- Radio may still stutter over Bluetooth when retransmissions accumulate.

Battery/current, clean systems, physical Apple Silicon/minimum-macOS and complete physical multiple-computer/device acceptance remain unverified. Automation does not measure input-method recognition accuracy or subjective listening quality.

## Build from source

Activate ESP-IDF 5.5.3 and run `./tools/validate.sh` for host tests, firmware build and merged-image verification. `./tools/validate.sh --firmware` runs only the firmware portion. On macOS, `bash macos/build.sh` creates universal packages and runs native tests. GitHub Actions rebuilds the release tag and uploads packages only after all release gates pass.
