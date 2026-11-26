import Foundation
import CoreFoundation

struct VoiceFrame {
    static let blockSize=VoiceCodec.blockBytes
    static let wireSize=1+8+4+blockSize
    static let maxFrames:UInt32=2000
    let session:UInt64,sequence:UInt32,block:Data
    static func parse(_ data:Data)->Self? {
        guard data.count==wireSize,data.first==6,data[15]<=88,data[16]==0 else {return nil}
        var session:UInt64=0,sequence:UInt32=0
        for i in 0..<8 {session |= UInt64(data[1+i])<<(i*8)}
        for i in 0..<4 {sequence |= UInt32(data[9+i])<<(i*8)}
        guard session != 0,sequence<maxFrames else {return nil}
        return Self(session:session,sequence:sequence,block:data.subdata(in:13..<wireSize))
    }
}

enum VoiceCapabilities {
    // Readiness and interleaved notification reassembly are independent.
    static let advertised:[String:Any]=["voiceV2":1,"voiceRepair":1,"voiceReady":1,"voiceInterleave":1,"voiceFeedback":1]
}

enum VoiceControl {
    static let repairHistory=2...30
    static let maxRepairBatch=4
    static func unsigned(_ value:Any?)->UInt32? {
        guard let number=value as? NSNumber,CFGetTypeID(number) != CFBooleanGetTypeID() else {return nil}
        let n=number.doubleValue
        guard n.isFinite,n>=0,n<=Double(UInt32.max),n.rounded()==n else {return nil}
        return UInt32(n)
    }
}

// A terminal event may beat start across links. Retain its ID so late copies
// cannot reopen a released recording. IDs are scoped to one device boot.
struct VoiceSessionEvents {
    private(set) var newest=0
    static func session(_ body:[String:Any])->UInt64? {
        guard body["voiceVersion"] as? Int==2,let text=body["voiceSession"] as? String,
              text.utf8.count==16,text.utf8.allSatisfy({(48...57).contains($0)||(97...102).contains($0)}),
              let value=UInt64(text,radix:16),value != 0 else {return nil}
        return value
    }
    mutating func accept(id:Int)->Bool {
        guard id>newest else {return false};newest=id;return true
    }
}

// Order input on the device's own timeline. A one-way hello cannot synchronize
// two clocks: transport delay, sleep and oscillator drift are indistinguishable.
// Its uptime is a watermark, never an absolute command deadline on the Mac.
struct DeviceEventTimeline {
    private var milliseconds:Double?
    private var observedAt:TimeInterval?
    static func valid(_ value:Double)->Bool { value.isFinite && value>=0 && value<9e15 }
    mutating func observe(milliseconds value:Double,now:TimeInterval) {
        guard Self.valid(value),now.isFinite,
              milliseconds.map({value>$0}) ?? true else{return}
        milliseconds=value;observedAt=now
    }
    func accepts(milliseconds value:Double)->Bool {
        guard let milliseconds,Self.valid(value) else{return false}
        // A newer authenticated observation can prove an event is old without
        // estimating clock offset. Out-of-order hello copies cannot lower it.
        return value>=milliseconds-VoicePolicy.commandReorderWindow*1000
    }
    func cancellationBoundary(now:TimeInterval)->Double? {
        guard let milliseconds,let observedAt else{return nil}
        // Used only to fence a cancellation, not to expire every later command.
        return milliseconds+max(0,now-observedAt)*1000
    }
}
