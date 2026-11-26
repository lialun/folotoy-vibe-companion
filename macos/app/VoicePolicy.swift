import Foundation

/// Local latency and resource budgets. Wire-format sizes live in VoiceCodec
/// and VoiceFrame; changing these policies does not change the protocol.
enum VoicePolicy {
    static let compressedCapacity:UInt32=32
    // Match the device's six-block repair horizon. A recording keeps this
    // budget when its useful carrier changes; the starting link mask is not
    // a promise about subsequent latency.
    static let prefill:TimeInterval=0.6
    static let gapWait:TimeInterval=0.12
    static let repairInterval:TimeInterval=0.08
    static let repairRetry:TimeInterval=0.35
    static let repairQueueLifetime:TimeInterval=0.3
    static let initialProbe:TimeInterval=0.25
    static let repairAttempts=2
    static let pcmCapacity=16000
    static let outputBufferSamples=320
    static let outputBufferCount=3
    static let audioTimeout:TimeInterval=4.5
    static let recordingLimit:TimeInterval=181
    static let tailTimeout:TimeInterval=2
    static let tickMilliseconds=20
    static let commandLifetime:TimeInterval=1
    static let commandReorderWindow:TimeInterval=1
    static let maxCommandHold:TimeInterval=1.5
    static let maxPendingCommands=4
}
