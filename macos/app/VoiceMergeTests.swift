import Foundation

enum VoiceMergeTests {
    static func run(_ check:(Bool,String)->Void) {
        let block=Data(repeating:0,count:804)
        func frame(_ sequence:UInt32,_ session:UInt64=0x123456789abcdef0)->VoiceFrame {
            VoiceFrame(session:session,sequence:sequence,block:block)
        }
        func wire(_ sequence:UInt32,_ session:UInt64=0x123456789abcdef0)->Data {
            var data=Data([6]);for i in 0..<8 {data.append(UInt8(truncatingIfNeeded:session>>(8*i)))}
            for i in 0..<4 {data.append(UInt8(truncatingIfNeeded:sequence>>(8*i)))}
            data.append(block);return data
        }
        let session=frame(0).session
        check(VoiceFrame.parse(wire(256))?.sequence==256,"V2 sequence survives legacy 8-bit wrap")
        check(VoiceFrame.parse(wire(256))?.session==session,"64-bit session uses firmware little endian")
        check(VoiceFrame.parse(wire(0,0))==nil && VoiceFrame.parse(wire(UInt32.max))==nil,"reject zero session and impossible sequence")
        var bad=wire(0);bad[15]=89;check(VoiceFrame.parse(bad)==nil,"validate ADPCM before buffering")
        check(VoiceFrame.parse(Data(wire(0).dropLast()))==nil,"reject truncated V2 packet")
        if case .voice(let decoded)?=Packet.parse(wire(42)) {check(decoded.sequence==42,"Packet dispatches V2 without premature decode")}
        else {check(false,"V2 wire dispatch")}
        var merge=VoiceMerge(session:session)
        check(!merge.accept(frame(0,1),via:.wifi,now:0),"ignore previous recording audio")
        check(merge.accept(frame(0),via:.wifi,now:0),"first copy accepted")
        check(!merge.accept(frame(0),via:.bluetooth,now:0.01),"duplicate is discarded before decode")
        check(merge.drain(now:0.119).isEmpty,"prefill provides bounded recovery time")
        check(merge.drain(now:0.12).count==1 && merge.next==1,"one block at first playout deadline")
        check(!merge.accept(frame(0),via:.bluetooth,now:0.13),"late duplicate cannot replay")
        check(merge.accept(frame(2),via:.wifi,now:0.2) && merge.accept(frame(1),via:.bluetooth,now:0.21),"alternate link repairs out-of-order hole")
        if case .audio(let f,.bluetooth)?=merge.drain(now:0.22).first {check(f.sequence==1,"repair plays once in order")}else{check(false,"repair output")}
        merge.finish(frames:3)
        check(merge.tailDelay(now:0.22)>=0.18 && merge.tailDelay(now:0.22)<=1,"terminal grace is bounded")
        check(merge.drain(now:0.32).count==1 && merge.drain(now:2).isEmpty,"end count prevents trailing silence")
        check(!merge.accept(frame(3),via:.wifi,now:2),"terminal count rejects post-release audio")
        check(merge.duplicates==1 && merge.late==2 && merge.recovered==1,"merge diagnostics distinguish duplicate, late and recovered")
        var startup=VoiceMerge(session:session)
        _=startup.accept(frame(1),via:.wifi,now:0);_=startup.accept(frame(0),via:.bluetooth,now:0.1)
        if case .audio(let f,_)?=startup.drain(now:0.12).first {check(f.sequence==0,"initial out-of-order block can recover during prefill")}
        else {check(false,"startup repair")}
        var leading=VoiceMerge(session:session);_=leading.accept(frame(3),via:.wifi,now:0)
        if case .audio(let f,_)?=leading.drain(now:0.12).first {check(f.sequence==3 && leading.missing==3,"skip missing leading blocks without adding startup latency")}
        else {check(false,"leading loss")}
        var bounded=VoiceMerge(session:session)
        for seq in UInt32(0)..<1500 { _=bounded.accept(frame(seq),via:.wifi,now:Double(seq)*0.1);check(bounded.buffered<=8,"stalled consumer has bounded compressed storage") }
        check(bounded.resyncs>0,"long stalls resynchronize instead of replaying a backlog")
        _=bounded.drain(now:1000000);check(bounded.next<=VoiceFrame.maxFrames,"large scheduler delay cannot overflow sequence")
        var empty=VoiceMerge(session:session);empty.finish(frames:0)
        check(empty.drain(now:100).isEmpty && !empty.accept(frame(0),via:.wifi,now:100),"zero-frame recording stays empty")
        var earlyEnd=VoiceMerge(session:session);earlyEnd.finish(frames:1)
        check(earlyEnd.tailDelay(now:0)>=0.36,"end beating first audio retains prefill, block duration and sink drain")
        _=earlyEnd.accept(frame(0),via:.bluetooth,now:0.03)
        check(earlyEnd.drain(now:0.15).count==1,"last short-recording block survives reordered end")
        var rescue=VoiceMerge(session:session)
        _=rescue.accept(frame(0),via:.wifi,now:0);_=rescue.drain(now:0.12)
        check(rescue.drain(now:0.22).isEmpty && rescue.next==1,"brief shared stall waits instead of dropping speech")
        _=rescue.accept(frame(1),via:.bluetooth,now:0.35)
        check(rescue.drain(now:0.36).count==1 && rescue.missing==0 && rescue.rescued==1,"late first copy can rescue a shared delay")
        check(rescue.extraDelay>0.13 && rescue.extraDelay<0.15,"only measured waiting extends latency")
        _=rescue.drain(now:0.46);_=rescue.drain(now:0.53)
        check(rescue.extraDelay<=VoiceMerge.rescueBudget && rescue.next>2,"permanent loss cannot wait indefinitely")
        var events=VoiceSessionEvents()
        check(events.accept(id:2) && !events.accept(id:1),"end preceding start prevents delayed automatic rerecording")
        check(events.accept(id:3) && !events.accept(id:3) && !events.accept(id:2),"control copies cannot affect a later session")
        check(VoiceSessionEvents.session(["voiceVersion":2,"voiceSession":"123456789abcdef0"])==session,"parse session control binding")
        check(VoiceSessionEvents.session(["voiceVersion":2,"voiceSession":"0000000000000000"])==nil,"reject empty nonce")
        var enter=VoiceEnterBuffer()
        check(enter.receive(recording:false,ending:false),"idle Enter remains immediate")
        check(!enter.receive(recording:true,ending:false) && enter.pending==0,"active speech does not insert Enter")
        for _ in 0..<2 {check(!enter.receive(recording:true,ending:true),"tail Enter waits for final audio")}
        check(enter.take(allowed:true)==2 && enter.pending==0,"preserve two quick presses after clean drain")
        for _ in 0..<100 {_=enter.receive(recording:true,ending:true)}
        check(enter.pending==8 && enter.take(allowed:false)==0 && enter.pending==0,"abnormal stop discards bounded pending Enter")

        // Deterministic two-link timeline through the real jitter buffer. Both
        // complementary loss and a single failed link must remain continuous.
        for scenario in 0..<5 {
            var simulated=VoiceMerge(session:session)
            var arrivals:[(Double,UInt32,ConnectionTransport)]=[]
            let count:UInt32=600
            for seq in UInt32(0)..<count {
                let t=Double(seq)*0.1
                if scenario != 1 && !(scenario==0 && seq%5==0) && !(scenario==3 && seq%35==0) {
                    arrivals.append((t+Double(seq%4)*0.009,seq,.wifi))
                }
                if scenario != 2 && !(scenario==0 && seq%5 != 0) && !(scenario==3 && seq%35==0) {
                    arrivals.append((t+0.025+Double(seq%6)*0.01,seq,.bluetooth))
                }
            }
            arrivals.sort{$0.0<$1.0};var index=0,played:[UInt32]=[];var silence=0
            for tick in 0...Int(count)*50+300 {
                let now=Double(tick)*0.002
                while index<arrivals.count,arrivals[index].0<=now {
                    let a=arrivals[index];_=simulated.accept(frame(a.1),via:a.2,now:now);index+=1
                }
                if now>=Double(count)*0.1 {simulated.finish(frames:count)}
                for out in simulated.drain(now:now) {
                    switch out {case .audio(let f,_):played.append(f.sequence);case .silence:silence+=1}
                }
                check(simulated.buffered<=8,"timeline buffer bound")
            }
            check(Set(played).count==played.count && played==played.sorted(),"each merged frame plays once in sequence")
            if scenario != 3 {check(played.count==Int(count) && silence==0,"continuous playback through complementary loss or one failed leg")}
            else {check(played.count==Int(count)-18 && simulated.missing==18,"shared losses are reported instead of claimed recovered")}
        }
        var delayed=VoiceMerge(session:session),played:[UInt32]=[]
        var packets:[(Double,UInt32)]=[]
        for seq in UInt32(0)..<100 {
            let delay:Double=seq==5 ? 0.25:(seq==50 ? 0.3:0)
            packets.append((Double(seq)*0.1+delay,seq))
        }
        packets.sort{$0.0<$1.0};var position=0
        for tick in 0...5500 {
            let now=Double(tick)*0.002
            while position<packets.count,packets[position].0<=now {
                let seq=packets[position].1
                _=delayed.accept(frame(seq),via:.wifi,now:now);_=delayed.accept(frame(seq),via:.bluetooth,now:now);position+=1
            }
            if now>=10 {delayed.finish(frames:100)}
            for out in delayed.drain(now:now) {if case .audio(let f,_)=out {played.append(f.sequence)}}
        }
        check(played==Array(UInt32(0)..<100) && delayed.missing==0,"shared 250/300 ms delay recovers every block once")
        check(delayed.extraDelay<=0.2 && delayed.rescued>=2,"adaptive latency has one fixed per-recording budget")
    }
}
