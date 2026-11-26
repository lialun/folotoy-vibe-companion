import Foundation

private final class FeedbackTestKeys:VoiceShortcut {
    var held=false
    func start(_ spec:String) throws->Bool {held=true;return true}
    func stop() {held=false}
}
enum VoiceFeedbackTests {
    static func run(_ check:@escaping (Bool,String)->Void) {
        let sample=VoiceFeedback(session:0x0102030405060708,id:17,retireBefore:3,received:0x80000005,finished:true)
        let packet=sample.packet(token:String(repeating:"0",count:64))!
        check(packet.count==54 && packet[0]==7 && packet[1]==0x81,"feedback wire size, type, version and terminal flag")
        check(Array(packet.suffix(20))==[8,7,6,5,4,3,2,1,17,0,0,0,3,0,0,0,5,0,0,128],"fixed cross-language little-endian feedback vector")
        check(sample.packet(token:"invalid")==nil && sample.packet(token:String(repeating:"g",count:64))==nil,"feedback requires full valid existing credential")
        var tracker=VoiceFeedbackTracker()
        check(tracker.next(session:1,retireBefore:0,received:1,now:0)?.id==1,"first receipt feedback")
        check(tracker.next(session:1,retireBefore:0,received:3,now:0.1)==nil,"coalesced feedback bounds production")
        check(tracker.next(session:1,retireBefore:1,received:1,now:0.2)?.id==2,"advancing feedback identity")
        check(tracker.next(session:1,retireBefore:2,received:0,now:0.21,finished:true)?.finished==true,"terminal feedback bypasses ordinary cadence")

        let output=TestVoiceOutput(),keys=FeedbackTestKeys()
        output.automaticDrain=false
        let voice=VoiceSession(output:output,shortcut:keys)
        var feedbacks:[VoiceFeedback]=[]
        voice.sendFeedback={ value in
            if value.finished {check(!keys.held && !output.running && output.isDrained,"terminal feedback follows key release and actual PCM drain")}
            feedbacks.append(value);return true
        }
        let body:[String:Any]=["voiceVersion":2,"voiceSession":"0000000000000001","voiceRequest":1,"voiceRepair":20,"voiceFeedback":1,"eid":1]
        check(voice.start(body,shortcut:"ctrl+option+v",now:0),"feedback-capable recording")
        func frame(_ sequence:UInt32)->VoiceFrame {VoiceFrame(session:1,sequence:sequence,block:Data(repeating:0,count:804))}
        voice.accept(frame(0),via:.wifi,now:0.1);voice.accept(frame(2),via:.bluetooth,now:0.2)
        voice.tick(now:0.2)
        check(feedbacks.last?.retireBefore==0 && feedbacks.last?.received==5,"selective receipt keeps a genuine hole unacknowledged")
        voice.end(["voiceVersion":2,"voiceSession":"0000000000000001","frames":3,"eid":2,"stop":0],now:0.3)
        voice.tick(now:0.72)
        voice.accept(frame(1),via:.bluetooth,now:0.8);voice.tick(now:0.82);voice.tick(now:1.0)
        check(voice.active && feedbacks.last?.retireBefore==3 && feedbacks.allSatisfy{!$0.finished},"receipt retirement alone cannot resume radio during queued PCM")
        output.consume();voice.tick(now:1.02)
        check(!voice.active && feedbacks.suffix(2).allSatisfy{$0.finished && $0.retireBefore==3 && $0.received==0},"bounded terminal copies return the arena after complete output")

        var buffer=BLEWriteBuffer()
        for i in 0..<100 {check(buffer.append([Data([UInt8(i)])],radio:false,voice:true,feedback:true),"feedback admission")}
        check(buffer.count==1 && buffer.first==Data([99]),"stalled reverse link retains only newest unstarted feedback")
        check(buffer.append([Data([100]),Data([101])],radio:false,voice:true,feedback:true),"fragmented feedback")
        _=buffer.acknowledge()
        check(buffer.append([Data([102])],radio:false,voice:true,feedback:true),"feedback while a prefix exists")
        check(buffer.count==2 && buffer.first==Data([101]),"coalescing preserves the active assembly")
    }
}
