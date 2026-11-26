import Foundation

enum VoiceMergeTests {
    static func run(_ check:(Bool,String)->Void) {
        let session:UInt64=0x123456789abcdef0
        func frame(_ seq:UInt32)->VoiceFrame {
            VoiceFrame(session:session,sequence:seq,block:Data(repeating:0,count:804))
        }
        func sequences(_ output:[VoiceMerge.Output])->[UInt32] {
            output.compactMap{if case .audio(let frame,_)=$0 {return frame.sequence};return nil}
        }
        var merge=VoiceMerge(session:session,prefill:0.25)
        check(merge.accept(frame(0),via:.wifi,now:0),"admit first block")
        check(!merge.accept(frame(0),via:.bluetooth,now:0.01) && merge.duplicates==1,"mirror is one logical block")
        check(merge.drain(now:0.24).isEmpty,"bounded initial jitter prefill")
        check(sequences(merge.drain(now:0.25))==[0],"first block plays once")
        for now in [0.5,1.0,10.0,100.0] {
            check(merge.drain(now:now).isEmpty && merge.next==1 && merge.missing==0,"clock cannot advance into unproduced speech")
        }
        check(merge.accept(frame(1),via:.bluetooth,now:100),"a delayed subsequent block remains usable")
        check(sequences(merge.drain(now:100))==[1] && merge.lateFirst==0,"recovery does not reapply startup prefill or replay earlier audio")
        merge.finish(frames:2)
        check(merge.complete && !merge.accept(frame(2),via:.wifi,now:101),"terminal count bounds the stream")

        var hole=VoiceMerge(session:session,repairHistory:6)
        _=hole.accept(frame(0),via:.wifi,now:0)
        _=hole.accept(frame(2),via:.bluetooth,now:0.2)
        hole.finish(frames:3)
        var heard:[UInt32]=[]
        for tick in 0...100 {heard+=sequences(hole.drain(now:Double(tick)*0.02))}
        check(heard==[0,2] && hole.missing==1 && hole.complete,"an observed permanent hole emits one bounded gap")
        let next=hole.next
        _=hole.drain(now:1000)
        check(hole.next==next && hole.missing==1,"completed stream cannot create more synthetic losses")

        var bounded=VoiceMerge(session:session)
        for sequence in UInt32(0)..<6 {_=bounded.accept(frame(sequence),via:.wifi,now:0)}
        check(bounded.drain(now:1,maximum:0).isEmpty && bounded.next==0,"full PCM ring cannot consume compressed frames")
        check(sequences(bounded.drain(now:1,maximum:2))==[0,1] && bounded.next==2,"handoff fits actual PCM headroom")
        check(sequences(bounded.drain(now:1,maximum:1))==[2],"consumer clock owns pacing without dropping queued speech")

        // Replay a persistent delay step. The old clock kept advancing through
        // the empty frontier, classifying every following block as late.
        for delay in [0.35,0.7] {
            var value=VoiceMerge(session:session,repairHistory:6)
            var position:UInt32=0,played:[UInt32]=[]
            for tick in 0...13000 {
                let now=Double(tick)*0.002
                while position<200,now>=Double(position)*0.1+(position>=20 ? delay:0) {
                    _=value.accept(frame(position),via:.bluetooth,now:now)
                    _=value.accept(frame(position),via:.wifi,now:now+0.001)
                    position+=1
                }
                if now>=20+delay {value.finish(frames:200)}
                played+=sequences(value.drain(now:now))
                check(value.buffered<=Int(VoiceMerge.capacity),"delay step retains a fixed buffer bound")
            }
            check(played==Array(UInt32(0)..<200) && value.missing==0,"persistent transit delay preserves all produced speech")
        }

        for scenario in 0..<5 {
            var value=VoiceMerge(session:session)
            var arrivals:[(at:Double,seq:UInt32,via:ConnectionTransport)]=[]
            for seq in UInt32(0)..<600 {
                let now=Double(seq)*0.1
                if scenario != 1 && !(scenario==0 && seq%3==0) {
                    arrivals.append((now+Double(seq%4)*0.009,seq,.wifi))
                }
                if scenario != 2 && !(scenario==0 && seq%3 != 0) {
                    arrivals.append((now+0.03+Double(seq%7)*0.012,seq,.bluetooth))
                }
            }
            arrivals.sort{$0.at<$1.at}
            var position=0,played:[UInt32]=[]
            for tick in 0...32000 {
                let now=Double(tick)*0.002
                while position<arrivals.count,arrivals[position].at<=now {
                    let event=arrivals[position]
                    _=value.accept(frame(event.seq),via:event.via,now:now)
                    position+=1
                }
                if now>=60 {value.finish(frames:600)}
                // One delayed host callback must not delete the speech that
                // was already delivered during that delay.
                if scenario==4 && now>=20 && now<20.6 {continue}
                played+=sequences(value.drain(now:now))
            }
            check(played==Array(UInt32(0)..<600) && value.missing==0,"single/dual carrier and host-delay timelines preserve ordered speech")
        }

        for via in [ConnectionTransport.wifi,.bluetooth] {
            let rtt:Double=via == .wifi ? 0.045:0.33
            var value=VoiceMerge(session:session,repairHistory:6)
            var repair=VoiceRepairTracker(started:0,history:6)
            var arrivals:[(Double,UInt32)]=[]
            for seq in UInt32(0)..<120 where seq%17>1 && seq != 119 {
                arrivals.append((Double(seq)*0.1+0.01,seq))
            }
            var played:[UInt32]=[]
            for tick in 0...8000 {
                let now=Double(tick)*0.002
                let ready=arrivals.filter{$0.0<=now}
                arrivals.removeAll{$0.0<=now}
                for (_,seq) in ready {_=value.accept(frame(seq),via:via,now:now)}
                if now>=12 {value.finish(frames:120)}
                if let request=repair.reserve(value,now:now) {
                    repair.complete(request.id,submission:.submitted,now:now)
                    for seq in request.frames where Double(seq)*0.1<=now && now-Double(seq)*0.1<=0.6 {
                        arrivals.append((now+rtt,seq))
                    }
                }
                played+=sequences(value.drain(now:now,queuedDuration:VoiceMerge.prefill))
            }
            check(played==Array(UInt32(0)..<120) && value.missing==0,"single-link repair covers leading, interior and terminal loss")
            check(repair.sent>0 && repair.sent<60,"repair has a bounded traffic budget")
        }
        testPlayoutReservoir(check)
        testCarrierDelayStep(check)
        testClock(check)
    }

    private static func testPlayoutReservoir(_ check:(Bool,String)->Void) {
        // CoreAudio continues consuming while the network worker is delayed.
        // A compressed prefill is useful only after it reaches the PCM queue.
        for prefill in [0.25,VoiceMerge.prefill] {
            let session:UInt64=100
            var merge=VoiceMerge(session:session,prefill:prefill)
            var produced:UInt32=0,queued=0,played=0,internalGap=0,pendingGap=0
            var heard=false,order:[UInt32]=[]
            for tick in 0...1600 {
                let now=Double(tick)*0.02
                let consumed=min(320,queued);queued-=consumed;played+=consumed
                if consumed>0 {internalGap+=pendingGap;pendingGap=0;heard=true}
                if heard {pendingGap+=320-consumed}
                let paused=now>=5 && now<5.2 || now>=10 && now<10.2 || now>=15 && now<15.2
                if paused {continue}
                while produced<300,now+0.000001>=Double(produced)*0.1+0.03 {
                    _=merge.accept(VoiceFrame(session:session,sequence:produced,block:Data(repeating:0,count:804)),via:.wifi,now:now)
                    produced+=1
                }
                if produced==300 {merge.finish(frames:300)}
                for output in merge.drain(now:now,maximum:(VoicePolicy.pcmCapacity-queued)/1600,queuedDuration:Double(queued)/16000) {
                    if case .audio(let frame,_)=output {order.append(frame.sequence)}
                    queued+=VoiceCodec.blockSamples
                }
            }
            check(internalGap==0,"prefill protects the actual PCM consumer across 200 ms network-worker pauses")
            check(order==Array(UInt32(0)..<300) && played==300*1600,"consumer-paced output preserves every sample and sequence")
        }
    }

    private static func testCarrierDelayStep(_ check:(Bool,String)->Void) {
        // A healthy second carrier can be slower than the first useful route.
        // The actual PCM clock must remain continuous when that delay appears;
        // retaining every compressed block alone is insufficient.
        func replay(prefill:TimeInterval)->(gap:Int,played:Int,ordered:Bool,missing:Int) {
            var merge=VoiceMerge(session:1,prefill:prefill)
            var next:UInt32=0,queued=0,played=0,order:[UInt32]=[]
            var continuity=PCMContinuity()
            for tick in 0...1600 {
                let now=Double(tick)*0.02,n=min(320,queued)
                queued-=n;played+=n;continuity.consume(n,requested:320)
                while next<300,now+0.000001>=Double(next)*0.1+0.03+(next>=50 ? 0.45:0) {
                    let frame=VoiceFrame(session:1,sequence:next,block:Data(repeating:0,count:804))
                    _=merge.accept(frame,via:next<50 ? .wifi:.bluetooth,now:now)
                    next+=1
                }
                if next==300 {merge.finish(frames:300)}
                for output in merge.drain(now:now,maximum:(VoicePolicy.pcmCapacity-queued)/1600,
                                           queuedDuration:Double(queued)/16000) {
                    if case .audio(let frame,_)=output {order.append(frame.sequence)}
                    queued+=VoiceCodec.blockSamples
                }
            }
            return (continuity.samples,played,order==Array(UInt32(0)..<300),merge.missing)
        }
        let old=replay(prefill:0.25),current=replay(prefill:VoicePolicy.prefill)
        check(old.gap>0 && old.missing==0,"negative control: complete packets can still leave PCM holes after a carrier delay step")
        check(current.gap==0 && current.played==300*1600 && current.ordered && current.missing==0,
              "one carrier-independent reservoir absorbs a 450 ms transit step without losing, repeating or changing samples")
    }

    private static func testClock(_ check:(Bool,String)->Void) {
        var clock=DeviceEventTimeline()
        check(!clock.accepts(milliseconds:1000),"input requires an observed device timeline")
        clock.observe(milliseconds:1000,now:100)
        check(clock.accepts(milliseconds:2000),"fresh input is ordered on the device timeline")
        clock.observe(milliseconds:4000,now:103)
        check(!clock.accepts(milliseconds:2000),"newer device metadata proves an old input is stale")
        clock.observe(milliseconds:2000,now:104)
        check(!clock.accepts(milliseconds:2000),"a delayed hello cannot make an old command fresh")
        var skew=DeviceEventTimeline();skew.observe(milliseconds:0,now:0)
        for second in 1...21600 {
            let now=Double(second),remote=now*0.998*1000
            skew.observe(milliseconds:remote,now:now)
            check(skew.accepts(milliseconds:remote+50),"six-hour independent clock drift retains fresh input")
        }
    }
}
