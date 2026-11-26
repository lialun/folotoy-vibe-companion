import Foundation

/// Bound whole-packet production in addition to CoreBluetooth's fragment
/// credits. Voice control bypasses background pacing; packets never interleave.
struct BLEWritePacing {
    enum Kind {case control,voice,radio}
    static let controlInterval:TimeInterval=0.2
    static let radioInterval:TimeInterval=0.05
    private var controlAt:TimeInterval = -.infinity
    private var radioAt:TimeInterval = -.infinity

    func delay(_ kind:Kind,now:TimeInterval)->TimeInterval {
        switch kind {
        case .control:return max(0,controlAt-now)
        case .radio:return max(0,radioAt-now)
        case .voice:return 0
        }
    }
    mutating func completed(_ kind:Kind,now:TimeInterval) {
        switch kind {
        case .control:controlAt=now+Self.controlInterval
        case .radio:radioAt=now+Self.radioInterval
        case .voice:break
        }
    }
}
