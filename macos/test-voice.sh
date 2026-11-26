#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
output="$(mktemp -d /tmp/vibe-voice-tests.XXXXXX)"
trap 'rm -rf -- "${output}"' EXIT
xcrun swiftc -swift-version 5 -Onone \
    "${root}/macos/app/VoicePolicy.swift" \
    "${root}/macos/app/Protocol.swift" \
    "${root}/macos/app/DeviceDiscovery.swift" \
    "${root}/macos/app/TransportPolicy.swift" \
    "${root}/macos/app/Shortcut.swift" \
    "${root}/macos/app/VoiceProtocol.swift" \
    "${root}/macos/app/VoiceFeedback.swift" \
    "${root}/macos/app/VoiceFeedbackTests.swift" \
    "${root}/macos/app/VoiceMerge.swift" \
    "${root}/macos/app/VoiceRepair.swift" \
    "${root}/macos/app/BLEWriteBuffer.swift" \
    "${root}/macos/app/BLEWriteFlight.swift" \
    "${root}/macos/app/BLEWritePacing.swift" \
    "${root}/macos/app/VoiceRepairTests.swift" \
    "${root}/macos/app/VoiceSession.swift" \
    "${root}/macos/app/InputCommands.swift" \
    "${root}/macos/app/PCMRing.swift" \
    "${root}/macos/app/PCMContinuity.swift" \
    "${root}/macos/app/VoiceMergeTests.swift" \
    "${root}/macos/app/VoiceSessionTests.swift" \
    "${root}/macos/app/InputCommandTests.swift" \
    "${root}/macos/app/BLEFrame.swift" \
    "${root}/macos/app/BLEFrameTests.swift" \
    "${root}/tests/macos/voice/main.swift" \
    -o "${output}/voice-tests"
"${output}/voice-tests"
xcrun swiftc -swift-version 5 -Onone \
    "${root}/macos/voice-bench/VoiceBenchPlan.swift" \
    "${root}/tests/macos/voice-bench/main.swift" -o "${output}/bench-plan-tests"
"${output}/bench-plan-tests"
