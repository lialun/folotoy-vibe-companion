import Foundation

private enum TestVoiceFailure:Error {case startup}

final class TestVoiceOutput:VoicePCMOutput {
    var running=false,reject=false,automaticDrain=true,failStart=false
    private var metrics=VoiceOutputMetrics()
    func start() throws {
        running=true;metrics=VoiceOutputMetrics()
        if failStart {throw TestVoiceFailure.startup}
    }
    func append(_ pcm:[Int16])->Bool {
        if reject {metrics.dropped+=pcm.count;return false}
        metrics.appended+=pcm.count
        if automaticDrain {metrics.rendered+=pcm.count} else {metrics.queued+=pcm.count}
        return true
    }
    func consume() {metrics.rendered+=metrics.queued;metrics.queued=0}
    func snapshot()->VoiceOutputMetrics {metrics}
    func stop() {running=false}
}

private final class TestVoiceShortcut:VoiceShortcut {
    var starts=0,stops=0
    var failStart=false,available=true
    func start(_ spec:String) throws->Bool {
        starts+=1
        if failStart {throw TestVoiceFailure.startup}
        return available
    }
    func stop() {stops+=1}
}

enum VoiceSessionTests {
    static func run(_ check:(Bool,String)->Void) {
        let output=TestVoiceOutput(),keyboard=TestVoiceShortcut()
        let voice=VoiceSession(output:output,shortcut:keyboard),input=InputCommands()
        var now=100.0,enters=0,commands:[[String:Any]]=[],results:[VoiceSessionResult]=[]
        input.observe(milliseconds:100000,now:now);input.strict=true
        input.postEnter={enters+=1;return true}
        voice.send={commands.append($0);return true}
        voice.sendRepair={body,complete in commands.append(body);complete(.submitted,now)}
        voice.onStarted={input.voiceStarted($0)}
        voice.onFinished={results.append($0);input.voiceFinished($0,now:now)}
        func start(_ session:UInt64,_ request:Int,_ eid:Int)->[String:Any] {
            ["event":"voice.start","voiceVersion":2,"voiceSession":String(format:"%016llx",session),
             "voiceRequest":request,"voiceRepair":6,"voiceLinks":3,"ready":true,"eid":eid]
        }
        func end(_ session:UInt64,_ frames:Int,_ eid:Int,_ stop:Int=0)->[String:Any] {
            ["event":"voice.end","voiceVersion":2,"voiceSession":String(format:"%016llx",session),
             "frames":frames,"eid":eid,"stop":stop]
        }
        func frame(_ session:UInt64,_ sequence:UInt32)->VoiceFrame {
            VoiceFrame(session:session,sequence:sequence,block:Data(repeating:0,count:804))
        }
        func tick(_ until:Double) {
            while now<until {now=min(until,now+0.02);voice.tick(now:now)}
        }
        func key(_ id:Int,_ request:Int,_ pressed:Double,_ released:Double)->[String:Any] {
            ["event":"key.action","action":"enter","inputVersion":1,"command":id,
             "voiceRequest":request,"pressedAtMs":pressed*1000,"atMs":released*1000]
        }

        check(voice.start(start(1,1,1),shortcut:"ctrl+option+v",now:now),"start owns one output/shortcut lifetime")
        check(!voice.start(start(1,1,2),shortcut:"ctrl+option+v",now:now),"repeated ready request acknowledges without restarting input")
        check(keyboard.starts==1 && commands.filter{$0["type"] as? String=="voice.ready"}.count==2,"readiness follows actual input startup and can be retried")
        now=100.1;voice.accept(frame(1,0),via:.wifi,now:now)
        voice.accept(frame(1,0),via:.bluetooth,now:now)
        tick(100.2);voice.accept(frame(1,1),via:.bluetooth,now:now)
        tick(100.6)
        input.receive(key(1,1,100.55,100.59),now:now)
        check(input.pendingCount==1 && enters==0,"explicit Enter before end is scoped and waits for output")
        voice.end(end(99,2,3),now:now)
        check(voice.active && !voice.isDraining,"stale recording end cannot stop current input")
        voice.end(end(1,2,4),now:now);tick(100.8)
        check(!voice.active && enters==1 && results.last?.clean==true,"one explicit intent follows a complete drained recording")
        input.receive(key(1,1,100.55,100.59),now:now)
        check(enters==1,"mirrored command identities inject exactly once")

        // Voice-only traces have no ability to call Enter, even through error,
        // replacement and a clean retry after an aborted recording.
        now=101
        check(voice.start(start(2,2,5),shortcut:"ctrl+option+v",now:now),"next recording starts")
        voice.accept(frame(2,0),via:.wifi,now:now)
        input.receive(key(2,2,100.95,100.99),now:now)
        voice.cancel(reason:"audio-timeout")
        check(enters==1 && input.pendingCount==0,"timeout destroys pending intent rather than submitting text")
        input.receive(key(3,2,100.96,100.995),now:now+0.01)
        check(enters==1,"a late intent pressed before failure cannot submit after timeout")
        now=101.2
        check(voice.start(start(3,3,6),shortcut:"ctrl+option+v",now:now),"clean retry has a new identity")
        input.receive(key(4,2,101.15,101.18),now:now)
        check(input.pendingCount==0 && enters==1,"previous recording command cannot enter a retry")
        voice.end(end(3,0,7),now:now);tick(101.3)
        check(!voice.active && results.last?.clean==false && enters==1,"empty voice does not submit anything")
        input.receive(key(5,3,101.31,101.35),now:101.36)
        check(enters==2,"a new explicit idle Enter still works after an empty recording")

        now=102
        voice.end(end(4,1,9),now:now)
        check(!voice.start(start(4,4,8),shortcut:"ctrl+option+v",now:now),"end-before-start cannot reopen input")
        check(voice.start(start(5,5,10),shortcut:"ctrl+option+v",now:now),"future recording remains admissible")
        voice.accept(frame(5,0),via:.bluetooth,now:now)
        output.automaticDrain=false
        voice.end(end(5,1,11),now:now)
        tick(102+VoicePolicy.prefill+0.05)
        check(voice.active && voice.isDraining,"shortcut stays held until CoreAudio has consumed its tail")
        output.consume();tick(now+0.05)
        check(!voice.active && results.last?.clean==true && enters==2,"draining output itself never manufactures Enter")
        output.automaticDrain=true

        now=103
        _=voice.start(start(6,6,12),shortcut:"ctrl+option+v",now:now)
        voice.accept(frame(6,0),via:.wifi,now:now)
        voice.end(end(6,1,13,3),now:now)
        input.receive(key(6,6,103.01,103.03),now:103.04)
        tick(103+VoicePolicy.prefill+0.1)
        check(results.last?.clean==false && enters==2,"source-error end cannot flush an Enter intent")
        now=104
        _=voice.start(start(7,7,14),shortcut:"ctrl+option+v",now:now)
        input.receive(key(7,7,104.01,104.04),now:104.05)
        voice.cancel(reason:"sleep");input.cancel(now:now)
        check(!output.running && !voice.active && input.pendingCount==0 && enters==2,"sleep tears down output/input without queued user activity")
        check(!VoiceSession.activityOptions.contains(.idleSystemSleepDisabled),"voice precision allows system sleep")

        // All lifecycle permutations without an explicit command must remain
        // unable to invoke Enter. Includes loss, reorder, replacement and retry.
        for iteration in 0..<200 {
            now=200+Double(iteration)*5
            let session=UInt64(100+iteration),eid=100+iteration*3
            _=voice.start(start(session,100+iteration,eid),shortcut:"ctrl+option+v",now:now)
            voice.accept(frame(session,0),via:.wifi,now:now)
            if iteration%3==0 {voice.cancel(reason:"lost-links")}
            else {
                voice.end(end(session,1,eid+1,iteration%3==1 ? 0:4),now:now)
                tick(now+VoicePolicy.prefill+0.1)
            }
            check(!voice.active && enters==2,"voice-only lifecycle has no Enter side effect")
        }
        testPCMRing(check)
        let overflow=VoiceSession(output:TestVoiceOutput(),shortcut:TestVoiceShortcut())
        var overflowResult:VoiceSessionResult?
        overflow.onFinished={overflowResult=$0}
        _=overflow.start(start(900,900,900),shortcut:"ctrl+option+v",now:0)
        overflow.accept(frame(900,0),via:.wifi,now:0)
        overflow.tick(now:0.3)
        overflow.accept(frame(900,100),via:.wifi,now:1)
        overflow.end(end(900,101,901),now:1)
        for tick in 0...150 {overflow.tick(now:1+Double(tick)*0.02)}
        check(overflowResult?.clean==false && (overflowResult?.missing ?? 0)>0,
              "buffer overflow loss is reflected in the session result and cannot count as a clean drain")
        for failure in 0..<3 {
            let sink=TestVoiceOutput(),keys=TestVoiceShortcut()
            sink.failStart=failure==0;keys.failStart=failure==1;keys.available=failure != 2
            let subject=VoiceSession(output:sink,shortcut:keys)
            var cancelled=0,started=0,finished=0,allFailed=true
            subject.send={body in if body["type"] as? String=="voice.cancel" {cancelled+=1};return true}
            subject.onStarted={_ in started+=1}
            subject.onFinished={result in finished+=1;allFailed = allFailed && !result.clean}
            check(!subject.start(start(901,901,901),shortcut:"ctrl+option+v",now:0),"partial startup fails explicitly")
            check(!subject.active && !sink.running && keys.stops==1 && started==0 && finished==1 && allFailed && cancelled==1,
                  "each partial startup rolls back output, keys, nonce and device capture through one terminal path")
            check(!subject.start(start(901,901,902),shortcut:"ctrl+option+v",now:1) && cancelled==1,
                  "a delayed start cannot reopen a failed recording")
        }
        let repairs=VoiceSession(output:TestVoiceOutput(),shortcut:TestVoiceShortcut())
        var repairIDs:[UInt32]=[]
        repairs.send={_ in true}
        var repairNow=0.0
        repairs.sendRepair={body,complete in
            if let id=VoiceControl.unsigned(body["repairID"]) {repairIDs.append(id)}
            complete(.submitted,repairNow)
        }
        _=repairs.start(start(903,903,903),shortcut:"ctrl+option+v",now:0)
        for at in [0.3,0.5,0.7] {repairNow=at;repairs.tick(now:at)}
        check(repairIDs==[1,2],"logical repair attempts have distinct recording-scoped identities for transport mirroring")
        repairs.cancel(reason:"test-complete")
        let delayed=VoiceSession(output:TestVoiceOutput(),shortcut:TestVoiceShortcut())
        delayed.send={_ in true}
        var completions=[VoiceRepairCompletion]()
        delayed.sendRepair={_,complete in completions.append(complete)}
        _=delayed.start(start(905,905,905),shortcut:"ctrl+option+v",now:0)
        delayed.tick(now:0.3);delayed.tick(now:0.6)
        check(completions.count==1,"a queued logical request cannot create concurrent retries")
        delayed.cancel(reason:"test-replaced")
        _=delayed.start(start(906,906,906),shortcut:"ctrl+option+v",now:2)
        delayed.tick(now:2.3);completions[0](.submitted,2.4);delayed.tick(now:2.5)
        check(completions.count==2,"old recording completion cannot release or charge the new request")
        completions[1](.notSubmitted,2.6);delayed.tick(now:2.7)
        check(completions.count==3,"expired unsent request remains retryable in the current recording")
        delayed.cancel(reason:"test-complete")
        let pacedOutput=TestVoiceOutput();pacedOutput.automaticDrain=false
        let paced=VoiceSession(output:pacedOutput,shortcut:TestVoiceShortcut())
        var pacedResult:VoiceSessionResult?
        paced.onFinished={pacedResult=$0}
        _=paced.start(start(904,904,904),shortcut:"ctrl+option+v",now:0)
        for sequence in UInt32(0)..<15 {paced.accept(frame(904,sequence),via:.wifi,now:0.01)}
        let primed=VoicePolicy.prefill+0.03
        paced.tick(now:primed);paced.tick(now:primed+0.02)
        check(paced.active && pacedOutput.snapshot().queued==VoicePolicy.pcmCapacity,
              "handoff stops at PCM headroom and retains the remaining compressed speech")
        pacedOutput.consume();paced.tick(now:primed+0.04)
        check(pacedOutput.snapshot().queued==5*1600,"new consumer capacity admits the retained tail in order")
        paced.end(end(904,15,905),now:primed+0.04);pacedOutput.consume();paced.tick(now:primed+0.06)
        check(pacedResult?.clean==true && pacedResult?.output.rendered==15*1600,
              "backpressure drains every sample without overflowing or truncating the recording")
        let tailOutput=TestVoiceOutput()
        let tail=VoiceSession(output:tailOutput,shortcut:TestVoiceShortcut())
        var tailResult:VoiceSessionResult?
        tail.onFinished={tailResult=$0}
        _=tail.start(start(907,907,907),shortcut:"ctrl+option+v",now:0)
        for sequence in UInt32(0)..<1799 {
            let at=Double(sequence)*0.1+0.01
            tail.accept(frame(907,sequence),via:.wifi,now:at);tail.tick(now:at)
        }
        tail.end(end(907,1800,908),now:180)
        // The final original packet arrives late, then CoreAudio still needs
        // its 100 ms. A normal end already transferred ownership of the clock
        // budget from capture to bounded draining.
        tailOutput.automaticDrain=false
        tail.accept(frame(907,1799),via:.bluetooth,now:180.95);tail.tick(now:180.95)
        check(tail.active && tailOutput.snapshot().queued==1600,"long recording retains its final pending output block")
        tail.tick(now:181.02)
        check(tail.active,"capture safety limit cannot truncate a valid bounded output tail after normal end")
        tailOutput.consume();tail.tick(now:181.08)
        check(tailResult?.clean==true && tailResult?.output.rendered==1800*1600,
              "a late final packet and its real output drain preserve the complete three-minute recording")
        let captureLimit=VoiceSession(output:TestVoiceOutput(),shortcut:TestVoiceShortcut())
        var limitReason=""
        captureLimit.onFinished={limitReason=$0.reason}
        _=captureLimit.start(start(908,908,908),shortcut:"ctrl+option+v",now:0)
        captureLimit.tick(now:181.02)
        check(!captureLimit.active && limitReason=="recording-limit","active capture still obeys its safety deadline")
        let stoppedOutput=TestVoiceOutput(),stoppedKeys=TestVoiceShortcut()
        let stopped=VoiceSession(output:stoppedOutput,shortcut:stoppedKeys)
        var stoppedResult:VoiceSessionResult?
        stopped.onFinished={stoppedResult=$0}
        _=stopped.start(start(902,902,902),shortcut:"ctrl+option+v",now:0)
        stoppedOutput.running=false
        stopped.tick(now:0.02)
        check(!stopped.active && stoppedResult?.reason=="output-stopped" && stoppedResult?.clean==false && stoppedKeys.stops==1,
              "an asynchronously failed audio adapter cancels input on the next clock turn")
    }

    private static func testPCMRing(_ check:(Bool,String)->Void) {
        var continuity=PCMContinuity()
        for _ in 0..<10 {continuity.consume(0,requested:320)}
        continuity.consume(320,requested:320)
        check(continuity.samples==0,"initial output silence is not a speech interruption")
        continuity.consume(0,requested:320);continuity.consume(0,requested:320)
        continuity.consume(160,requested:320)
        check(continuity.samples==640 && continuity.longest==640 && continuity.interruptions==1,
              "a resumed consumer commits exactly the intervening empty samples")
        continuity.consume(320,requested:320)
        check(continuity.samples==800 && continuity.interruptions==2,"partial output starvation is measured without inspecting PCM values")
        for _ in 0..<20 {continuity.consume(0,requested:320)}
        check(continuity.samples==800,"an empty drained tail does not inflate midstream gaps")
        var ring=PCMRing(capacity:8)
        var buffer=[Int16](repeating:0,count:8)
        check(ring.append([1,2,3,4,5,6]),"PCM accepts within capacity")
        let n=buffer.withUnsafeMutableBufferPointer{ring.read(into:$0.baseAddress!,maximum:4)}
        check(n==4 && Array(buffer.prefix(4))==[1,2,3,4],"PCM FIFO output")
        check(ring.append([7,8,9,10,11,12]) && !ring.append([13]),"PCM overload cannot clear preceding speech")
        let count=buffer.withUnsafeMutableBufferPointer{ring.read(into:$0.baseAddress!,maximum:8)}
        check(count==8 && buffer==[5,6,7,8,9,10,11,12],"wrapped PCM preserves exact order after overload")
    }
}
