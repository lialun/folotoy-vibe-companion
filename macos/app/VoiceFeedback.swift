import Foundation

/// Acknowledges receiver-owned blocks, separately from no-longer-useful
/// encoded history. Completion means PCM drained and the shortcut released.
struct VoiceFeedback:Equatable {
    static let wireSize=54
    let session:UInt64,id:UInt32,retireBefore:UInt32,received:UInt32
    var finished=false
    func packet(token:String)->Data? {
        guard token.utf8.count==64,session != 0,id != 0 else{return nil}
        let text=Array(token.utf8)
        func nibble(_ byte:UInt8)->UInt8? {
            if (48...57).contains(byte) {return byte-48}
            if (97...102).contains(byte) {return byte-87}
            return nil
        }
        var bytes=Data([7,finished ? 0x81:1])
        for i in 0..<32 {
            guard let high=nibble(text[2*i]),let low=nibble(text[2*i+1]) else{return nil}
            bytes.append(high<<4|low)
        }
        for i in 0..<8 {bytes.append(UInt8(truncatingIfNeeded:session>>(8*i)))}
        for value in [id,retireBefore,received] {
            for i in 0..<4 {bytes.append(UInt8(truncatingIfNeeded:value>>(8*i)))}
        }
        return bytes
    }
}

struct VoiceFeedbackTracker {
    private var sequence:UInt32=0
    private var lastSent:TimeInterval = -.infinity
    mutating func next(session:UInt64,retireBefore:UInt32,received:UInt32,now:TimeInterval,finished:Bool=false)->VoiceFeedback? {
        guard finished || now-lastSent>=0.2 else{return nil}
        sequence+=1;lastSent=now
        return VoiceFeedback(session:session,id:sequence,retireBefore:retireBefore,received:received,finished:finished)
    }
}
