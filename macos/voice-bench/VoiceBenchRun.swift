import Foundation

final class VoiceBenchRun {
    static weak var current:VoiceBenchRun?
    static var enterCount=0
    let plan:VoiceBenchPlan
    private let destination:URL
    private let sourceVersion:String,sourceDigest:String
    private var stages:[VoiceBenchPlan.Stage] {plan.stages}
    var onProgress:((String)->Void)?,onFinished:(()->Void)?,onSequence:((Int)->Void)?
    private enum Phase {case settle,configure,radioWarmup,start,awaitRecording,recording,stop,draining,done}
    private var phase:Phase = .settle
    private var index=0,change=0,sequence:Int
    private var connectedAt:Double?,started=0.0,phaseAt=0.0,opened=ProcessInfo.processInfo.systemUptime
    private var pending:([String:Any],Double,Int,()->Void)?
    private var result:VoiceSessionResult?,stats:[String:Any]?
    private var identity:VoiceIdentity?,received:Set<UInt32>=[],played:Set<UInt32>=[]
    private var receivedHash:UInt32=0,playedHash:UInt32=0,ordered=true
    private var repairFirstSent:[UInt32:Double]=[:],repairResponses:[Double]=[]
    private var starts=0
    private var radioEnabled=false
    private var lastArrival:Double?,maxArrivalGap=0.0,minTransit=Double.infinity,maxTransit = -Double.infinity
    private var repairSubmitted=0,repairUnsent=0,repairUncertain=0,repairMaxQueue=0.0
    private var rows:[[String:Any]]=[],lastNoise=0.0,lastPause=0.0,pauses=0,noise=0
    var send:(([String:Any])->Bool)?
    init(plan:VoiceBenchPlan,destination:URL,lastSequence:Int=0,sourceVersion:String?=nil,sourceDigest:String?=nil) {
        self.sourceVersion=sourceVersion ?? Bundle.main.object(forInfoDictionaryKey:"VoiceBenchCoreVersion") as? String ?? "unknown"
        self.sourceDigest=sourceDigest ?? Bundle.main.object(forInfoDictionaryKey:"VoiceBenchSourceDigest") as? String ?? "unknown"
        self.plan=plan;self.destination=destination;self.sequence=max(Int(Date().timeIntervalSince1970),lastSequence);Self.current=self;Self.enterCount=0
    }
    func cancel() {finish("cancelled")}
    func voiceBegan(_ value:VoiceIdentity) {identity=value;starts+=1}
    func repairCompleted(_ result:VoiceRepairSubmission,wait:Double,frames:[UInt32],at:Double) {
        switch result {case .submitted:repairSubmitted+=1;case .notSubmitted:repairUnsent+=1;case .uncertain:repairUncertain+=1}
        repairMaxQueue=max(repairMaxQueue,wait)
        if result != .notSubmitted {for sequence in frames where !received.contains(sequence) && repairFirstSent[sequence]==nil {repairFirstSent[sequence]=at}}
    }
    func received(_ frame:VoiceFrame) {
        guard frame.session==identity?.session,received.insert(frame.sequence).inserted else {return}
        let now=ProcessInfo.processInfo.systemUptime
        if let lastArrival {maxArrivalGap=max(maxArrivalGap,now-lastArrival)}
        if let sent=repairFirstSent.removeValue(forKey:frame.sequence) {repairResponses.append(max(0,now-sent)*1000)}
        lastArrival=now
        let transit=now-Double(frame.sequence)*VoiceMerge.period
        minTransit=min(minTransit,transit);maxTransit=max(maxTransit,transit)
        receivedHash ^= Self.hash(frame)
    }
    func played(_ frame:VoiceFrame) {
        guard frame.session==identity?.session else {return}
        let next=UInt32(played.count),inserted=played.insert(frame.sequence).inserted
        ordered = ordered && frame.sequence==next && inserted
        playedHash ^= Self.hash(frame)
    }
    private static func hash(_ frame:VoiceFrame)->UInt32 {
        var bytes=Data()
        for i in 0..<8 {bytes.append(UInt8(truncatingIfNeeded:frame.session>>(8*i)))}
        for i in 0..<4 {bytes.append(UInt8(truncatingIfNeeded:frame.sequence>>(8*i)))}
        bytes.append(frame.block)
        return bytes.reduce(UInt32(2166136261)){($0 ^ UInt32($1)) &* 16777619}
    }
    func reply(_ body:[String:Any]) {
        guard let item=pending,body["id"] as? String==item.0["id"] as? String else {return}
        pending=nil
        if body["ok"] as? Bool != true {finish("device rejected fixture command");return}
        item.3()
    }
    func ended(_ value:VoiceSessionResult) {result=value}
    func diagnostics(_ body:[String:Any]) {
        guard body["voiceSession"] as? String==identity?.session.map({String(format:"%016llx",$0)}) else {return}
        let allowed:Set<String>=["event","voiceSession","frames","captureMs","captureGapMs","sendGapMs","stop","tx","radio","net","feedback","fixtureHash","fixtureInjected","fixtureMemory","fixtureKeys","fixtureWiFi","fixtureRepair","fixturePower","fixtureSubmit"]
        stats=body.filter{allowed.contains($0.key)}
    }
    func tick(connected:Bool,recording:Bool) {
        let now=ProcessInfo.processInfo.systemUptime
        guard phase != .done else {return}
        if now-opened>plan.observationLimit {finish("bounded matrix deadline");return}
        if let item=pending {
            if now-item.1>0.8 {
                if item.2>=8 {finish("fixture command acknowledgement timeout");return}
                _=send?(item.0);pending=(item.0,now,item.2+1,item.3)
            }
            return
        }
        let stage=stages[index]
        switch phase {
        case .settle:
            if connected {
                if connectedAt==nil {connectedAt=now}
                if now-connectedAt!>=5 {phase = .configure}
            } else {connectedAt=nil}
            if now-opened>plan.connectionTimeout {finish("required authenticated control links were not ready")}
        case .configure:
            if stage.radio != radioEnabled {
                command("radio",extra:["enabled":stage.radio,"url":plan.radioURL ?? ""]) {[weak self] in
                    guard let self else{return};self.radioEnabled=stage.radio
                    self.phase = .radioWarmup;self.phaseAt=ProcessInfo.processInfo.systemUptime
                }
                return
            }
            result=nil;stats=nil;identity=nil;received=[];played=[];receivedHash=0;playedHash=0;ordered=true
            change=0;lastNoise=now;lastPause=now;pauses=0;noise=0;starts=0
            lastArrival=nil;maxArrivalGap=0;minTransit = .infinity;maxTransit = -.infinity
            repairSubmitted=0;repairUnsent=0;repairUncertain=0;repairMaxQueue=0;repairFirstSent=[:];repairResponses=[]
            command("mask",mask:stage.mask,fault:stage.fault) {[weak self] in self?.phase = .start}
        case .radioWarmup:
            if now-phaseAt>=4 {phase = .configure}
        case .start:
            command("start") {[weak self] in self?.phase = .awaitRecording;self?.phaseAt=ProcessInfo.processInfo.systemUptime}
        case .awaitRecording:
            if recording {started=now;phase = .recording;persist()}
            else if now-phaseAt>5 {finish("desktop did not begin recording")}
        case .recording:
            let elapsed=now-started
            if !recording && !stage.outage {phase = .stop;return}
            if stage.controls,now-lastNoise>=2 {
                lastNoise=now
                for _ in 0..<4 {_=send?(["type":"config.get","id":UUID().uuidString.lowercased()]);noise+=1}
            }
            if stage.hostPause,now-lastPause>=5 {lastPause=now;pauses+=1;Thread.sleep(forTimeInterval:0.2)}
            if change<stage.changes.count,elapsed>=stage.changes[change].at {
                let mask=stage.changes[change].mask;change+=1;command("mask",mask:mask) {}
            } else if elapsed>=stage.seconds {phase = .stop}
        case .stop:
            command("stop") {[weak self] in self?.phase = .draining;self?.phaseAt=ProcessInfo.processInfo.systemUptime}
        case .draining:
            if let result,!recording {
                if now-phaseAt<(stage.radio ? 4:1) {return}
                if stats==nil,now-phaseAt<4 {return}
                let captured=stats?["frames"] as? Int
                let sourceHash=(stats?["fixtureHash"] as? NSNumber)?.uint32Value
                let injected=stats?["fixtureInjected"] as? Int ?? 0
                let output=result.output
                let integrity=captured==result.accepted && captured==received.count && captured==played.count &&
                    sourceHash==receivedHash && sourceHash==playedHash && ordered &&
                    output.fixtureAppendHash==output.fixtureReadHash
                let passed=Self.enterCount==0 && starts==1 && (stage.outage ?
                    result.reason=="audio-timeout" && !result.clean :
                    result.clean && result.accepted>=Int(stage.seconds*8) && result.missing==0 &&
                    output.appended==result.accepted*1600 && output.rendered==output.appended && output.interruptedSamples==0 &&
                    integrity && (stage.fault==0 || injected>0))
                var row:[String:Any]=["case":stage.name,"pass":passed,"reason":result.reason,"clean":result.clean,
                    "accepted":result.accepted,"missing":result.missing,"appended":output.appended,"rendered":output.rendered,
                    "underrun":output.underrun,"discarded":output.dropped,"enter":Self.enterCount,"mask":stage.mask,
                    "fault":stage.fault,"injected":injected,"receivedUnique":received.count,"playedUnique":played.count,
                    "ordered":ordered,"receivedHash":Int(receivedHash),"playedHash":Int(playedHash),
                    "pcmAppendHash":Int(output.fixtureAppendHash),"pcmReadHash":Int(output.fixtureReadHash),
                    "internalGapSamples":output.interruptedSamples,"maxGapSamples":output.longestInterruption,
                    "arrivalMaxGapMs":maxArrivalGap*1000,"transitVariationMs":received.isEmpty ? 0:(maxTransit-minTransit)*1000,
                    "repairResponseMs":repairResponses,"repairSubmitted":repairSubmitted,"repairUnsent":repairUnsent,"repairUncertain":repairUncertain,"repairMaxQueueMs":repairMaxQueue*1000,
                    "hostPauses":pauses,"controlRequests":noise,"recordingStarts":starts,"requiresBothControlLinks":plan.transport == .dual,"applicationTransportMode":plan.transport.rawValue]
                if let stats {row["source"]=stats}
                for key in ["captureMs","captureGapMs","sendGapMs","stop","tx","radio","net","fixtureWiFi"] {if let value=stats?[key] {row[key]=value}}
                if let captured {row["captured"]=captured}
                if let sourceHash {row["sourceHash"]=Int(sourceHash)}
                rows.append(row);guard persist() else{return}
                index+=1
                if index==stages.count {finish(rows.allSatisfy{$0["pass"] as? Bool==true} ? nil:"one or more case assertions failed")} else {phase = .configure}
            } else if now-phaseAt>6 {finish("terminal result missing")}
        case .done:break
        }
    }
    private func command(_ operation:String,mask:Int?=nil,fault:Int?=nil,extra:[String:Any]=[:],completion:@escaping ()->Void) {
        sequence+=1;onSequence?(sequence)
        var body:[String:Any]=["type":"voice.fixture","operation":operation,"seq":sequence,"id":UUID().uuidString.lowercased()]
        if let mask {body["mask"]=mask};if let fault {body["fault"]=fault}
        for (key,value) in extra {body[key]=value}
        pending=(body,ProcessInfo.processInfo.systemUptime,1,completion);_=send?(body)
    }
    private func finish(_ error:String?) {
        guard phase != .done else {return};phase = .done;pending=nil
        for operation in ["mask","stop","radio"] {
            sequence+=1;onSequence?(sequence);_=send?(["type":"voice.fixture","operation":operation,"mask":3,"fault":0,"enabled":false,"seq":sequence,"id":UUID().uuidString.lowercased()])
        }
        if persist(error:error) {onFinished?()}
    }
    @discardableResult private func persist(error:String?=nil)->Bool {
        var body:[String:Any]=["completed":phase == .done,"pass":phase == .done && error==nil && rows.count==stages.count,
            "caseIndex":index,"totalCases":stages.count,"currentCase":index<stages.count ? stages[index].name:"done",
            "elapsedSeconds":ProcessInfo.processInfo.systemUptime-opened,"results":rows,"keyboardIsMock":true,
            "fullGUI":false,"productionEngine":true,"mediaMasksOnly":true,"normalSourceVersion":sourceVersion,
            "sourceDigest":sourceDigest,"applicationTransportMode":plan.transport.rawValue]
        if let error {body["error"]=error}
        do {
            let data=try JSONSerialization.data(withJSONObject:body,options:[.prettyPrinted,.sortedKeys])
            try data.write(to:destination,options:.atomic)
            onProgress?(phase == .done ? (error ?? "All stages passed") : "\(index+1)/\(stages.count): \(stages[index].name)")
            return true
        } catch {
            phase = .done;pending=nil;onProgress?("Cannot write results: \(error.localizedDescription)")
            onFinished?();return false
        }
    }
}
