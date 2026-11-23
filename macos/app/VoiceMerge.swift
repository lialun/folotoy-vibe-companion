import Foundation

struct VoiceFrame {
    static let wireSize=817,blockSize=804
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

// Compressed blocks live on the computer. Playout deadlines, rather than the
// arrival order of either link, decide when a block becomes irrecoverably late.
struct VoiceMerge {
    static let capacity:UInt32=8,prefill:TimeInterval=0.12,period:TimeInterval=0.1
    static let rescueBudget:TimeInterval=0.2
    enum Output {case audio(VoiceFrame,ConnectionTransport),silence(UInt32)}
    let session:UInt64
    private var blocks:[UInt32:(VoiceFrame,ConnectionTransport)]=[:]
    private var holes:Set<UInt32>=[]
    private var seen:Set<UInt32>=[]
    private var deadline:TimeInterval?,endCount:UInt32?
    private var played=false
    private var waiting=false
    private(set) var extraDelay:TimeInterval=0,rescued=0
    private(set) var next:UInt32=0,accepted=0,duplicates=0,late=0,recovered=0,missing=0,resyncs=0
    private(set) var wifiCopies=0,bleCopies=0
    private(set) var lateFirst=0
    var received:Int {seen.count}
    var buffered:Int {blocks.count}
    init(session:UInt64) {self.session=session}
    mutating func accept(_ frame:VoiceFrame,via source:ConnectionTransport,now:TimeInterval)->Bool {
        guard frame.session==session,frame.block.count==VoiceFrame.blockSize,
              frame.sequence<VoiceFrame.maxFrames else {return false}
        if source == .wifi {wifiCopies+=1}else{bleCopies+=1}
        if endCount.map({frame.sequence >= $0})==true {late+=1;return false}
        let first=seen.insert(frame.sequence).inserted
        if frame.sequence<next {late+=1;if first {lateFirst+=1};return false}
        if blocks[frame.sequence] != nil {duplicates+=1;return false}
        if frame.sequence-next>=Self.capacity {
            // A long outage must not turn into seconds of stale playback.
            missing+=Int(frame.sequence-next);next=frame.sequence;resyncs+=1
            blocks.removeAll(keepingCapacity:true);holes.removeAll(keepingCapacity:true)
            deadline=now+Self.prefill
            waiting=false
        }
        if deadline==nil {deadline=now+Self.prefill}
        if frame.sequence>next {
            for seq in next..<frame.sequence where blocks[seq]==nil {holes.insert(seq)}
        }
        if holes.remove(frame.sequence) != nil {recovered+=1}
        blocks[frame.sequence]=(frame,source);accepted+=1
        return true
    }
    mutating func finish(frames:UInt32) {
        guard frames<=VoiceFrame.maxFrames else {return}
        endCount=frames;blocks=blocks.filter{$0.key<frames};holes=holes.filter{$0<frames}
    }
    mutating func drain(now:TimeInterval)->[Output] {
        guard var due=deadline,now>=due,next<(endCount ?? VoiceFrame.maxFrames) else {return []}
        if !played,let first=blocks.keys.min(),first>next {
            missing+=Int(first-next);next=first
        }
        played=true
        if waiting {
            // Both paths can stall together. Spend a bounded additional delay
            // rather than throwing away speech that arrives just after due.
            let delay=min(max(0,now-due),max(0,Self.rescueBudget-extraDelay))
            if blocks[next] != nil || delay>=Self.rescueBudget-extraDelay-0.000001 {
                if blocks[next] != nil {rescued+=1}
                extraDelay+=delay;due+=delay;waiting=false
            } else {return []}
        }
        if now-due>0.3 {
            let limit=endCount ?? VoiceFrame.maxFrames
            let count=UInt32(min(floor((now-due)/Self.period),Double(limit>next ? limit-next:0)))
            next+=count;missing+=Int(count);resyncs+=1;due+=Double(count)*Self.period
            blocks=blocks.filter{$0.key>=next};holes=holes.filter{$0>=next}
        }
        var out:[Output]=[]
        while now+0.000001>=due,out.count<2,next<(endCount ?? VoiceFrame.maxFrames) {
            if blocks[next]==nil,extraDelay<Self.rescueBudget-0.000001 {
                waiting=true;break
            }
            if let (frame,source)=blocks.removeValue(forKey:next) {out.append(.audio(frame,source))}
            else {out.append(.silence(next));missing+=1}
            holes.remove(next);next+=1;due+=Self.period
        }
        deadline=due;return out
    }
    func tailDelay(now:TimeInterval)->TimeInterval {
        guard let end=endCount,end>next else {return 0.18}
        // End can arrive before the first audio copy on the other link.
        guard let due=deadline else {return 0.36}
        // Includes the last 100 ms block and the CoreAudio output buffers.
        let rescue=max(0,Self.rescueBudget-extraDelay)
        return min(1,max(0.36,due-now+Double(end-next)*Self.period+rescue+0.08))
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

// A quick Enter after release must follow the drained audio. Never replay it
// after disconnect, sleep, permission loss or a recording timeout.
struct VoiceEnterBuffer {
    private(set) var pending=0
    mutating func receive(recording:Bool,ending:Bool)->Bool {
        if !recording {return true}
        if ending {pending=min(8,pending+1)}
        return false
    }
    mutating func take(allowed:Bool)->Int {
        let count=allowed ? pending:0;pending=0;return count
    }
}
