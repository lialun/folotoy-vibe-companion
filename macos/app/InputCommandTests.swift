import Foundation

enum InputCommandTests {
    static func run(_ check:(Bool,String)->Void) {
        let input=InputCommands()
        input.strict=true
        input.observe(milliseconds:1000,now:100)
        var sent=0
        input.postEnter={sent+=1;return true}
        let first=VoiceIdentity(session:1,request:1),second=VoiceIdentity(session:2,request:2)
        func key(_ id:Int,_ request:Int,_ press:Double,_ release:Double)->[String:Any] {
            ["action":"enter","inputVersion":1,"command":id,"voiceRequest":request,
             "pressedAtMs":(press-99)*1000,"atMs":(release-99)*1000]
        }
        func result(_ identity:VoiceIdentity,_ clean:Bool)->VoiceSessionResult {
            VoiceSessionResult(identity:identity,reason:clean ? "released":"audio-timeout",
                clean:clean,accepted:1,missing:clean ? 0:1,output:VoiceOutputMetrics())
        }
        input.voiceStarted(first)
        input.receive(key(1,1,101,101.05),now:101.1)
        input.voiceFinished(result(first,true),now:103)
        check(sent==0 && input.pendingCount==0,"expired intent cannot submit after a delayed clean finish")
        input.voiceStarted(second)
        input.receive(key(2,2,104,104.05),now:104.06)
        input.voiceFinished(result(first,true),now:104.07)
        check(input.pendingCount==1 && sent==0,"old completion cannot flush another recording's intent")
        input.voiceFinished(result(second,false),now:104.1)
        input.receive(key(3,2,104.05,104.15),now:104.16)
        check(sent==0,"intent pressed before failure cannot become an idle Enter after failure")
        input.receive(key(4,2,104.2,104.25),now:104.3)
        check(sent==1,"new independent idle command remains usable after failure")
        input.voiceStarted(first)
        input.receive(key(5,2,105,105.05),now:105.06)
        check(input.pendingCount==0,"recording request mismatch is rejected before queueing")
        var malformed=key(6,1,105.1,105.15);malformed.removeValue(forKey:"pressedAtMs")
        input.receive(malformed,now:105.16)
        input.receive(key(7,1,105.2,105.19),now:105.21)
        input.observe(milliseconds:11000,now:110)
        input.receive(key(8,1,105.3,105.4),now:110)
        var invalid=key(9,1,111,111.1);invalid["atMs"]=Double.infinity
        input.receive(invalid,now:110)
        check(input.pendingCount==0 && sent==1,"missing, reversed, proven-old and nonfinite timestamps cannot inject input")
        for id in 10...14 {input.receive(key(id,1,110,110.05),now:110.06)}
        check(input.pendingCount==4 && sent==1,"pending commands have a fixed bound")
        input.voiceFinished(result(first,true),now:110.1)
        check(sent==5 && input.pendingCount==0,"distinct valid presses are consumed once after their own clean drain")
        input.cancel(now:111)
        input.receive(key(15,1,110.9,111.1),now:111.15)
        check(sent==5,"disconnect/sleep cannot replay a press from before cancellation")
        input.receive(key(16,99,111.2,111.25),now:111.3)
        check(sent==6,"new idle Enter after reconnect is independent of previous recording configuration")
        input.resetScope()
        input.receive(key(1,1,112,112.05),now:112.1)
        check(sent==6,"new device scope requires its own clock observation")
        input.resetScope();input.observe(milliseconds:0,now:112)
        input.receive(["action":"enter","inputVersion":1,"command":1,"voiceRequest":0,
                       "pressedAtMs":10.0,"atMs":40.0],now:112.05)
        check(sent==7,"a reboot may restart command identities in its new scope")
        input.receive(["action":"enter","eid":50],now:112.1)
        check(sent==7,"modern firmware cannot fall back to unscoped input commands")
        input.strict=false
        input.receive(["action":"enter","eid":51],now:112.1)
        check(sent==8,"old firmware retains immediate idle Enter without a deferred queue")
        testIndependentClocks(check)
    }

    private static func testIndependentClocks(_ check:(Bool,String)->Void) {
        let input=InputCommands()
        input.strict=true
        var posted=0
        input.postEnter={posted+=1;return true}
        input.observe(milliseconds:0,now:0)
        // A one-way clock estimator capped at 200 ppm rejects every fresh
        // command after this valid long-running clock divergence.
        input.observe(milliseconds:7_185_000,now:7200)
        let wire="""
        {"action":"enter","inputVersion":1,"command":1,"voiceRequest":0,"pressedAtMs":7185010,"atMs":7185050}
        """
        let command=try! JSONSerialization.jsonObject(with:Data(wire.utf8)) as! [String:Any]
        input.receive(command,now:7200.08)
        input.receive(command,now:7200.09)
        check(posted==1,"fresh idle Enter survives independent clock drift and mirrored delivery through real JSON parsing")

        // A slow hello cannot make older source events fresh again.
        input.observe(milliseconds:7_170_000,now:7201)
        var stale=command;stale["command"]=2;stale["pressedAtMs"]=7_170_010.0;stale["atMs"]=7_170_050.0
        input.receive(stale,now:7201.1)
        check(posted==1,"older metadata and stale commands cannot move the device watermark backward")

        let identity=VoiceIdentity(session:99,request:99)
        input.voiceStarted(identity)
        var queued=command;queued["command"]=3;queued["voiceRequest"]=99
        queued["pressedAtMs"]=7_185_100.0;queued["atMs"]=7_185_150.0
        input.receive(queued,now:7201.2)
        input.voiceFinished(VoiceSessionResult(identity:identity,reason:"released",clean:true,
            accepted:1,missing:0,output:VoiceOutputMetrics()),now:7201.3)
        check(posted==2,"queued Enter uses local waiting time rather than a converted remote deadline")

        input.observe(milliseconds:7_190_000,now:7205)
        input.cancel(now:7205.1)
        var cancelled=command;cancelled["command"]=4
        cancelled["pressedAtMs"]=7_190_050.0;cancelled["atMs"]=7_190_150.0
        input.receive(cancelled,now:7205.2)
        check(posted==2,"cancellation still fences a gesture begun before the cancellation boundary")
        var fresh=cancelled;fresh["command"]=5;fresh["pressedAtMs"]=7_190_200.0;fresh["atMs"]=7_190_250.0
        input.receive(fresh,now:7205.3)
        check(posted==3,"new deliberate Enter remains usable after cancellation with divergent clocks")
    }
}
