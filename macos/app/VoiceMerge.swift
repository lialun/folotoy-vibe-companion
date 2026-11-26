import Foundation

/// A bounded sequence buffer. After initial prefill, ordered speech moves to
/// the PCM consumer, whose hardware clock sets the playback rate. Unknown
/// future sequence numbers never become synthetic losses.
struct VoiceMerge {
    static let capacity=VoicePolicy.compressedCapacity
    static let period=TimeInterval(VoiceCodec.blockSamples)/VoiceCodec.sampleRate
    static let prefill=VoicePolicy.prefill
    static let repairWait=VoicePolicy.gapWait
    enum Output {case audio(VoiceFrame,ConnectionTransport),silence(UInt32)}

    let session:UInt64
    let repairHistory:Int
    private let startupPrefill:TimeInterval
    private var blocks:[UInt32:(VoiceFrame,ConnectionTransport)]=[:]
    private var seen:Set<UInt32>=[]
    private var highest:UInt32?,endCount:UInt32?
    private var readyAt:TimeInterval?,holeSince:TimeInterval?,holeDeadline:TimeInterval?
    private var primed=false
    private(set) var next:UInt32=0
    private(set) var accepted=0,duplicates=0,late=0,lateFirst=0,missing=0,recovered=0
    private(set) var wifiCopies=0,bleCopies=0,resyncs=0
    private(set) var maxWait:TimeInterval=0
    var received:Int {seen.count}
    var buffered:Int {blocks.count}
    var complete:Bool {endCount.map{next >= $0} ?? false}
    var receiptMask:UInt32 {
        blocks.keys.reduce(UInt32(0)) {mask,sequence in
            sequence>=next && sequence-next<32 ? mask | (UInt32(1)<<(sequence-next)):mask
        }
    }

    init(session:UInt64,repairHistory:Int=0,prefill:TimeInterval?=nil) {
        self.session=session
        self.repairHistory=repairHistory
        startupPrefill=prefill ?? Self.prefill
    }

    @discardableResult
    mutating func accept(_ frame:VoiceFrame,via source:ConnectionTransport,now:TimeInterval)->Bool {
        guard frame.session==session,frame.block.count==VoiceFrame.blockSize,
              frame.sequence<VoiceFrame.maxFrames,
              endCount.map({frame.sequence<$0}) ?? true else {return false}
        if source == .wifi {wifiCopies+=1} else {bleCopies+=1}
        let first=seen.insert(frame.sequence).inserted
        if !first {duplicates+=1;return false}
        if frame.sequence<next {late+=1;lateFirst+=1;return false}
        if frame.sequence-next>=Self.capacity {
            let floor=frame.sequence-Self.capacity+1
            let retained=UInt32(max(1,repairHistory))
            let repairFloor=max(floor,frame.sequence+1>retained ? frame.sequence+1-retained:0)
            let available=blocks.keys.filter{$0>=floor}.min() ?? frame.sequence
            let resume=max(floor,min(available,repairFloor))
            missing+=Int(resume-next)
            next=resume
            blocks=blocks.filter{$0.key>=resume}
            holeSince=nil;holeDeadline=nil
            resyncs+=1
        }
        if let highest,frame.sequence<highest {recovered+=1}
        highest=max(highest ?? frame.sequence,frame.sequence)
        blocks[frame.sequence]=(frame,source)
        accepted+=1
        if !primed,readyAt==nil {readyAt=now+startupPrefill}
        return true
    }

    mutating func finish(frames:UInt32) {
        guard frames<=VoiceFrame.maxFrames,endCount==nil || endCount==frames else {return}
        endCount=frames
        blocks=blocks.filter{$0.key<frames}
    }

    private var frontier:UInt32 {endCount ?? highest.map{$0+1} ?? 0}

    func repairCandidates(now:TimeInterval,started:TimeInterval,history:Int)->[UInt32] {
        let upper=min(frontier,next+Self.capacity)
        if highest==nil,endCount==nil,now-started>=VoicePolicy.initialProbe {return [0]}
        guard history>=2,upper>next else {return []}
        let lower=max(next,frontier>UInt32(history) ? frontier-UInt32(history):0)
        guard lower<upper else {return []}
        return (lower..<upper).filter{blocks[$0]==nil}.prefix(VoiceControl.maxRepairBatch).map{$0}
    }

    mutating func drain(now:TimeInterval,maximum:Int=Int(Self.capacity),queuedDuration:TimeInterval=0)->[Output] {
        guard maximum>0 else {return []}
        guard next<frontier else {
            // Do not reapply startup delay after a temporary empty frontier.
            // Already-buffered PCM continues to protect the consumer.
            holeSince=nil;holeDeadline=nil
            return []
        }
        if !primed {
            guard let readyAt,now+0.000001>=readyAt else {return []}
            primed=true
        }
        var output:[Output]=[]
        while output.count<min(maximum,Int(Self.capacity)),next<frontier {
            if let (frame,source)=blocks.removeValue(forKey:next) {
                output.append(.audio(frame,source))
                holeSince=nil;holeDeadline=nil
            } else {
                if holeSince==nil {
                    holeSince=now
                    // Repair may use the speech already ahead of this hole
                    // in CoreAudio, plus the existing bounded extra wait.
                    holeDeadline=now+max(0,queuedDuration)+Double(output.count)*Self.period+Self.repairWait
                }
                let wait=max(0,now-(holeSince ?? now))
                maxWait=max(maxWait,wait)
                guard now+0.000001>=(holeDeadline ?? now) else {break}
                output.append(.silence(next))
                missing+=1
                holeSince=nil;holeDeadline=nil
            }
            next+=1
        }
        return output
    }
}
