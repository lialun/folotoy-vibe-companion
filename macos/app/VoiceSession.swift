import Foundation

struct VoiceOutputMetrics {
#if VIBE_VOICE_BENCH
    var fixtureAppendHash:UInt32=2166136261,fixtureReadHash:UInt32=2166136261
#endif
    var appended=0,rendered=0,underrun=0,dropped=0,highWater=0,queued=0,inFlight=0
    var interruptions=0,interruptedSamples=0,longestInterruption=0
}

protocol VoicePCMOutput:AnyObject {
    var running:Bool {get}
    func start() throws
    @discardableResult func append(_ pcm:[Int16])->Bool
    func snapshot()->VoiceOutputMetrics
    func stop()
}

extension VoicePCMOutput {
    var isDrained:Bool {let value=snapshot();return value.queued==0 && value.inFlight==0}
}

protocol VoiceShortcut:AnyObject {
    @discardableResult func start(_ spec:String) throws->Bool
    func stop()
}

struct VoiceIdentity:Equatable {
    let session:UInt64?
    let request:UInt32?
}

struct VoiceSessionResult {
    let identity:VoiceIdentity
    let reason:String
    let clean:Bool
    let accepted:Int,missing:Int
    let output:VoiceOutputMetrics
}

/// Serialized by its owner. No transport selection, pairing, UI, or Enter
/// injection lives here. The same state machine runs with a fake clock/output.
final class VoiceSession {
    static let activityOptions:ProcessInfo.ActivityOptions=[.userInitiatedAllowingIdleSystemSleep,.latencyCritical]
    private enum Phase {case capturing,draining(deadline:TimeInterval,normal:Bool)}
    private final class Recording {
        let identity:VoiceIdentity
        let started:TimeInterval
        var lastAudio:TimeInterval
        var phase:Phase = .capturing
        var merge:VoiceMerge?
        var repair:VoiceRepairTracker?
        var feedback:VoiceFeedbackTracker?
        var legacySequence:UInt8?
        var accepted=0,missing=0
        var clockAt:TimeInterval?,maxClockGap:TimeInterval=0
        var activity:NSObjectProtocol?
        init(identity:VoiceIdentity,now:TimeInterval) {
            self.identity=identity;started=now;lastAudio=now
        }
    }

    private let output:VoicePCMOutput
    private let shortcut:VoiceShortcut
    private var recording:Recording?
    private var events=VoiceSessionEvents()
    private var closed:[UInt64]=[]
    private(set) var legacyLoss=VoiceLossWindow()
    var send:(([String:Any])->Bool)?
    var sendRepair:(([String:Any],@escaping VoiceRepairCompletion)->Void)?
    var sendFeedback:((VoiceFeedback)->Bool)?
    var beforeStart:(()->Void)?
    var onStarted:((VoiceIdentity)->Void)?
    var onFinished:((VoiceSessionResult)->Void)?
    var onProgress:((Int,Int)->Void)?
    var onError:((String)->Void)?
    var log:((String)->Void)?

    init(output:VoicePCMOutput,shortcut:VoiceShortcut) {
        self.output=output;self.shortcut=shortcut
    }

    var active:Bool {recording != nil}
    var identity:VoiceIdentity? {recording?.identity}
    var isDraining:Bool {
        guard let recording else {return false}
        if case .draining=recording.phase {return true}
        return false
    }
    var usesLegacy:Bool {recording?.identity.session==nil}
    var lastAudioDate:Date {
        guard let value=recording else {return .distantPast}
        return Date().addingTimeInterval(value.lastAudio-ProcessInfo.processInfo.systemUptime)
    }
    var startedDate:Date? {
        recording.map{Date().addingTimeInterval($0.started-ProcessInfo.processInfo.systemUptime)}
    }
    func resetLegacyLoss() {legacyLoss.reset(now:Date())}
    func hasSevereLegacyLoss(now:Date)->Bool {legacyLoss.severe(now:now)}

    func resetScope() {
        cancel(reason:"device-changed")
        events=VoiceSessionEvents();closed=[]
    }

    @discardableResult
    func start(_ body:[String:Any],shortcut spec:String,now:TimeInterval)->Bool {
        let request=VoiceControl.unsigned(body["voiceRequest"])
        guard body["voiceRequest"]==nil || request != nil else {return false}
        let session:UInt64?
        if body["voiceVersion"] != nil {
            guard let nonce=VoiceSessionEvents.session(body),let id=body["eid"] as? Int else {return false}
            if recording?.identity.session==nonce {
                if !isDraining,recording?.identity.request==request {acknowledgeStart(body,session:nonce)}
                return false
            }
            guard !closed.contains(nonce),events.accept(id:id) else {return false}
            session=nonce
        } else {
            guard !active else {return false}
            session=nil
        }
        cancel(reason:"replaced")
        beforeStart?()
        let value=Recording(identity:VoiceIdentity(session:session,request:request),now:now)
        // Admission owns cleanup even before output/shortcut startup finishes.
        // Partial startup follows the same cancellation path as a live session.
        recording=value
        do {
            try output.start()
            guard try shortcut.start(spec) else {cancel(reason:"input-unavailable");return false}
        } catch {
            onError?(error.localizedDescription)
            cancel(reason:"startup-failed")
            return false
        }
        if let session {
            let history=body["voiceRepair"] as? Int ?? 0
            let count=VoiceControl.repairHistory.contains(history) ? history:0
            value.merge=VoiceMerge(session:session,repairHistory:count)
            if count>0 {
                value.repair=VoiceRepairTracker(started:now,history:count)
                if body["voiceFeedback"] as? Int==1 {value.feedback=VoiceFeedbackTracker()}
            }
        }
        value.activity=ProcessInfo.processInfo.beginActivity(options:Self.activityOptions,reason:"Streaming voice input")
        legacyLoss.reset(now:Date())
        onStarted?(value.identity)
        log?("Voice started session=\(session.map{String(format:"%016llx",$0)} ?? "legacy") request=\(request.map{String($0)} ?? "legacy")")
        if let session {acknowledgeStart(body,session:session)}
        return true
    }

    private func acknowledgeStart(_ body:[String:Any],session:UInt64) {
        if body["ready"] as? Bool==true {
            _=send?(["type":"voice.ready","voiceSession":String(format:"%016llx",session)])
        }
    }

    func accept(_ frame:VoiceFrame,via source:ConnectionTransport,now:TimeInterval) {
        guard let value=recording else {return}
        let before=value.merge?.missing ?? 0
        guard value.merge?.accept(frame,via:source,now:now)==true else {return}
        let lost=(value.merge?.missing ?? 0)-before
        value.lastAudio=now;value.accepted+=1
        value.missing+=lost
        onProgress?(1,lost)
        requestRepair(value,now:now)
    }

    func acceptLegacy(sequence:UInt8,pcm:[Int16],now:TimeInterval) {
        guard let value=recording,value.identity.session==nil else {return}
        var missing=0
        if let previous=value.legacySequence {
            let delta=sequence &- previous
            guard delta>0,delta<=128 else {return}
            missing=Int(delta)-1
            if missing>0,!output.append([Int16](repeating:0,count:min(3,missing)*VoiceCodec.blockSamples)) {
                cancel(reason:"output-overflow");return
            }
        }
        guard output.append(pcm) else {cancel(reason:"output-overflow");return}
        value.legacySequence=sequence;value.lastAudio=now
        value.accepted+=1;value.missing+=missing
        legacyLoss.record(missing:missing)
        onProgress?(1,missing)
    }

    func end(_ body:[String:Any],now:TimeInterval) {
        let stop=body["stop"]==nil ? 0:VoiceControl.unsigned(body["stop"])
        guard let stop,stop<=7 else {return}
        let request=VoiceControl.unsigned(body["voiceRequest"])
        guard body["voiceRequest"]==nil || request != nil else {return}
        if body["voiceVersion"] != nil {
            guard let session=VoiceSessionEvents.session(body),let id=body["eid"] as? Int,
                  let frames=VoiceControl.unsigned(body["frames"]),frames<=VoiceFrame.maxFrames else {return}
            _=events.accept(id:id)
            guard let value=recording,value.identity.session==session else {rememberClosed(session);return}
            if request != nil,request != value.identity.request {return}
            value.merge?.finish(frames:frames)
        } else if recording?.identity.session != nil {return}
        guard let value=recording else {return}
        guard case .capturing=value.phase else {return}
        let normal=stop==0
        value.phase = .draining(deadline:now+VoicePolicy.tailTimeout,normal:normal)
        requestRepair(value,now:now)
    }

    func tick(now:TimeInterval,permitted:Bool=true) {
        guard let value=recording else {return}
        if !permitted {cancel(reason:"permission-lost");return}
        if !output.running {cancel(reason:"output-stopped");return}
        if case .capturing=value.phase,now-value.started>=VoicePolicy.recordingLimit {
            cancel(reason:"recording-limit");return
        }
        if let previous=value.clockAt {value.maxClockGap=max(value.maxClockGap,now-previous)}
        value.clockAt=now
        requestRepair(value,now:now)
        if var merge=value.merge {
            let before=merge.missing
            let pending=output.snapshot()
            let free=max(0,VoicePolicy.pcmCapacity-pending.queued)
            let frames=merge.drain(now:now,maximum:free/VoiceCodec.blockSamples,
                queuedDuration:Double(pending.queued+pending.inFlight)/VoiceCodec.sampleRate)
            value.merge=merge
            value.missing+=merge.missing-before
            if merge.missing>before {onProgress?(0,merge.missing-before)}
            for frame in frames {
                let pcm:[Int16]?
                switch frame {
                case .audio(let block,_):
#if VIBE_VOICE_BENCH
                    VoiceBenchRun.current?.played(block)
#endif
                    pcm=VoiceCodec.decode(block.block)
                case .silence:pcm=[Int16](repeating:0,count:VoiceCodec.blockSamples)
                }
                guard let pcm,output.append(pcm) else {cancel(reason:"output-overflow");return}
            }
        }
        updateFeedback(value,now:now)
        switch value.phase {
        case .capturing:
            if now-value.lastAudio>VoicePolicy.audioTimeout {cancel(reason:"audio-timeout")}
        case .draining(let deadline,let normal):
            let sourceDrained=value.merge?.complete ?? true
            if sourceDrained && output.isDrained {
                finish(reason:normal ? "released":"device-aborted",normal:normal)
            } else if now>=deadline {cancel(reason:"tail-timeout")}
        }
    }

    private func requestRepair(_ value:Recording,now:TimeInterval) {
        guard let sendRepair,let merge=value.merge,let session=value.identity.session,
              let request=value.repair?.reserve(merge,now:now) else {return}
        sendRepair(["type":"voice.repair","voiceSession":String(format:"%016llx",session),
                    "repairID":Int(request.id),"frames":request.frames.map{Int($0)}]) { [weak self,weak value] result,completedAt in
            guard let self,let value,self.recording===value else {return}
            value.repair?.complete(request.id,submission:result,now:completedAt)
#if VIBE_VOICE_BENCH
            VoiceBenchRun.current?.repairCompleted(result,wait:max(0,completedAt-now),frames:request.frames,at:completedAt)
#endif
        }
    }

    func cancel(reason:String) {finish(reason:reason,normal:false)}

    private func updateFeedback(_ value:Recording,now:TimeInterval,finished:Bool=false) {
        guard let session=value.identity.session,let merge=value.merge,
              let feedback=value.feedback?.next(session:session,retireBefore:merge.next,
                  received:merge.receiptMask,now:now,finished:finished) else{return}
        _=sendFeedback?(feedback)
        if finished {_=sendFeedback?(feedback)} // Bounded terminal redundancy.
    }

    private func rememberClosed(_ session:UInt64) {
        if !closed.contains(session) {closed.append(session)}
        if closed.count>8 {closed.removeFirst()}
    }

    private func finish(reason:String,normal:Bool) {
        guard let value=recording else {return}
        recording=nil
        if let session=value.identity.session {rememberClosed(session)}
        if !normal,let session=value.identity.session {
            _=send?(["type":"voice.cancel","voiceSession":String(format:"%016llx",session)])
        }
        shortcut.stop()
        let metrics=output.snapshot()
        output.stop()
        if normal {updateFeedback(value,now:ProcessInfo.processInfo.systemUptime,finished:true)}
        if let activity=value.activity {ProcessInfo.processInfo.endActivity(activity)}
        let clean=normal && value.accepted>0 && value.missing==0 &&
            (value.merge?.missing ?? 0)==0 && metrics.dropped==0 &&
            (value.merge?.lateFirst ?? 0)==0 && metrics.queued==0 && metrics.inFlight==0
        if let merge=value.merge {
            log?("Voice merge accepted=\(merge.accepted) received=\(merge.received) duplicate=\(merge.duplicates) late_first=\(merge.lateFirst) missing=\(merge.missing) wifi_copies=\(merge.wifiCopies) ble_copies=\(merge.bleCopies) wait_ms=\(Int(merge.maxWait*1000))")
        }
        log?("Voice stopped reason=\(reason) clean=\(clean) appended=\(metrics.appended) rendered=\(metrics.rendered) discarded=\(metrics.dropped) clock_gap_ms=\(Int(value.maxClockGap*1000)) output_gaps=\(metrics.interruptions) output_gap_ms=\(Int(Double(metrics.interruptedSamples)*1000/VoiceCodec.sampleRate)) output_max_gap_ms=\(Int(Double(metrics.longestInterruption)*1000/VoiceCodec.sampleRate)) repair_submitted=\(value.repair?.sent ?? 0) repair_unsent=\(value.repair?.rejected ?? 0)")
        onFinished?(VoiceSessionResult(identity:value.identity,reason:reason,clean:clean,
            accepted:value.accepted,missing:value.missing,output:metrics))
    }

    deinit {
        shortcut.stop();output.stop()
        if let activity=recording?.activity {ProcessInfo.processInfo.endActivity(activity)}
    }
}

extension Shortcut:VoiceShortcut {}
