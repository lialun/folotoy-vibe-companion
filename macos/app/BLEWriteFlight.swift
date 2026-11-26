import Foundation

/// Serializes acknowledged ATT writes. A retry delay owns no in-flight
/// fragment; replacing its queue head invalidates the old retry timer.
struct BLEWriteFlight {
    private enum Phase {case idle,inFlight,backoff}
    private var phase:Phase = .idle
    private var generation:UInt64=0
    var busy:Bool {phase != .idle}
    var inFlight:Bool {phase == .inFlight}
    mutating func begin() {generation &+= 1;phase = .inFlight}
    mutating func complete() {phase = .idle}
    mutating func deferRetry()->UInt64 {phase = .backoff;return generation}
    mutating func resume(_ token:UInt64)->Bool {
        guard phase == .backoff,generation==token else {return false}
        phase = .idle;return true
    }
    mutating func headRemoved() {if phase == .backoff {reset()}}
    mutating func reset() {generation &+= 1;phase = .idle}
}
