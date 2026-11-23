import Foundation

// A healthy attachment keeps the session. Preference is only for initial
// discovery; a restored Wi-Fi heartbeat must never steal a working BLE stream.
struct TransportPolicy {
    static let silence:TimeInterval=6,voiceStall:TimeInterval=3,hold:TimeInterval=15
    private(set) var switchedAt=Date.distantPast
    struct Change { let transport:ConnectionTransport,reason:String }
    mutating func select(current:ConnectionTransport,wifi:Date,ble:Date,now:Date,
                         voiceStarted:Date?,audio:Date,severeLoss:Bool=false)->Change? {
        let currentSeen=current == .wifi ? wifi:ble
        let otherSeen=current == .wifi ? ble:wifi
        let other:ConnectionTransport=current == .wifi ? .bluetooth:.wifi
        // The alternative must have delivered recent authenticated traffic.
        guard now>=otherSeen,now.timeIntervalSince(otherSeen)<3 else {return nil}
        let dead=currentSeen == .distantPast || now.timeIntervalSince(currentSeen)>=Self.silence
        let stalled=voiceStarted.map { start in
            now.timeIntervalSince(max(start,max(audio,switchedAt)))>=Self.voiceStall
        } ?? false
        guard dead || ((stalled || severeLoss) && now.timeIntervalSince(switchedAt)>=Self.hold) else {return nil}
        switchedAt=now
        return Change(transport:other,reason:dead ? "link-unavailable":stalled ? "audio-stalled":"audio-loss")
    }
    mutating func reset() { switchedAt = .distantPast }
}

struct VoiceLossWindow {
    private var began:Date?,received=0,missing=0,bad=false
    private(set) var percent=0
    mutating func reset(now:Date) {began=now;received=0;missing=0;bad=false;percent=0}
    mutating func record(missing gap:Int) {received+=1;missing+=gap}
    mutating func severe(now:Date)->Bool {
        guard let began=began else {return false}
        if now<began {reset(now:now);return false}
        if now.timeIntervalSince(began)>=5 {
            let total=received+missing
            percent=total>0 ? missing*100/total:0
            bad=total>=20 && percent>=70
            self.began=now;received=0;missing=0
        }
        return bad
    }
}

// A failed ATT write has not consumed the queued fragment. Retry that exact
// fragment with bounded backoff; exhaustion can reconnect without erasing trust.
struct BLEWriteRetry {
    private(set) var attempts=0
    mutating func delay()->TimeInterval? {
        let delays:[TimeInterval]=[0.1,0.25,0.5,1]
        guard attempts<delays.count else {return nil}
        defer {attempts+=1};return delays[attempts]
    }
    mutating func reset() {attempts=0}
}
