import Foundation

enum VoiceRepairTests {
    static func run(_ check:(Bool,String)->Void) {
        var merge=VoiceMerge(session:1,repairHistory:6)
        for sequence in [UInt32(0),2] {_=merge.accept(VoiceFrame(session:1,sequence:sequence,block:Data(repeating:0,count:804)),via:.bluetooth,now:0)}
        var tracker=VoiceRepairTracker(started:0,history:6),queue=BLEWriteBuffer()
        _=queue.append([Data([9])],radio:false)
        for now in [0.2,0.42] {
            guard let request=tracker.reserve(merge,now:now) else {check(false,"an unsent expired request can reserve another attempt");return}
            check(request.frames==[1],"repair reserves only an observed hole")
            check(queue.append([Data([1]),Data([2])],radio:false,repair:.init(id:UInt64(request.id),deadline:now+0.2),writing:true),"repair is locally admitted behind control")
            check(tracker.reserve(merge,now:now+0.1)==nil,"one logical request owns all pending fragments")
            let expired=queue.discardExpiredRepairs(now:now+0.21)
            check(!expired.headRemoved && expired.repairs.count==1,"expiry reports the queued request once, leaving control untouched")
            for result in expired.repairs {tracker.complete(UInt32(result.id),submission:result.submission,now:now+0.21)}
        }
        guard let actual=tracker.reserve(merge,now:0.7) else {check(false,"two unsent expiries do not exhaust the frame's delivery budget");return}
        check(actual.id==3 && tracker.sent==0 && tracker.rejected==2,"only actual submissions consume attempts")
        tracker.complete(actual.id,submission:.submitted,now:1)
        check(tracker.reserve(merge,now:1.1)==nil,"retry delay starts at completion, not queue admission")
        guard let second=tracker.reserve(merge,now:1.4) else {check(false,"a submitted request permits its bounded second attempt");return}
        tracker.complete(actual.id,submission:.notSubmitted,now:1.41)
        check(tracker.pending?.id==second.id,"duplicate or stale completions cannot release a newer reservation")
        tracker.complete(second.id,submission:.uncertain,now:1.5)
        check(tracker.reserve(merge,now:2)==nil && tracker.sent==2,"an ambiguous full-packet write is conservatively charged")

        var bleRecovery=VoiceRepairTracker(started:0,history:6)
        if let request=bleRecovery.reserve(merge,now:0.2) {
            bleRecovery.complete(request.id,submission:.submitted,now:0.2)
            check(bleRecovery.reserve(merge,now:0.34)==nil,"BLE recovery does not duplicate a still returning 330 ms repair after 120 ms")
            _=merge.accept(VoiceFrame(session:1,sequence:1,block:Data(repeating:0,count:804)),via:.bluetooth,now:0.53)
            check(bleRecovery.reserve(merge,now:0.56)==nil,"a recovered BLE block needs no second repair")
        } else {check(false,"BLE recovery can reserve its initial request")}

        for a in [VoiceRepairSubmission.submitted,.notSubmitted,.uncertain] {
            for b in [VoiceRepairSubmission.submitted,.notSubmitted,.uncertain] {
                for firstLeg in [ConnectionTransport.wifi,.bluetooth] {
                    var receipt=VoiceRepairReceipt(wifi:true,bluetooth:true)
                    let other:ConnectionTransport=firstLeg == .wifi ? .bluetooth:.wifi
                    let results=[receipt.complete(firstLeg,a),receipt.complete(other,b)].compactMap{$0}
                    let expected:VoiceRepairSubmission=a == .submitted || b == .submitted ? .submitted:
                        a == .uncertain || b == .uncertain ? .uncertain:.notSubmitted
                    check(results==[expected],"every mirrored completion ordering resolves once with conservative uncertainty")
                    check(receipt.complete(firstLeg,.notSubmitted)==nil,"duplicate mirror completion cannot refund a committed request")
                }
            }
        }
        var flight=BLEWriteFlight()
        flight.begin();let oldRetry=flight.deferRetry()
        check(flight.busy && !flight.inFlight,"backoff serializes writes without pretending a fragment is in flight")
        flight.headRemoved();check(!flight.busy,"removing a queued backoff head releases the write pump")
        flight.begin()
        check(!flight.resume(oldRetry) && flight.inFlight,"cancelled retry timer cannot release a newer ATT write")
        flight.complete();flight.begin();let validRetry=flight.deferRetry()
        check(flight.resume(validRetry) && !flight.resume(validRetry),"one valid retry token resumes once")
        flight.begin();flight.reset()
        check(!flight.busy && !flight.resume(validRetry),"disconnect invalidates old write retry work")
        var priority=BLEWriteBuffer()
        _=priority.append([Data([1]),Data([2])],radio:false)
        _=priority.append([Data([3])],radio:false)
        _=priority.append([Data([4]),Data([5])],radio:false,repair:.init(id:99,deadline:1),writing:true)
        check(priority.first==Data([1]),"repair cannot interrupt the current control assembly")
        priority.acknowledge();priority.acknowledge()
        check(priority.first==Data([4]),"short-lived repair bypasses only the unstarted control backlog")
        priority.acknowledge();_=priority.acknowledge()
        check(priority.first==Data([3]),"ordinary control progresses immediately after one repair")
        var voiceControl=BLEWriteBuffer()
        _=voiceControl.append([Data([1])],radio:false)
        _=voiceControl.append([Data([2])],radio:false,voice:true)
        _=voiceControl.append([Data([3])],radio:false,voice:true)
        check(voiceControl.first==Data([2]),"ready and cancel bypass unstarted background controls")
        voiceControl.acknowledge()
        check(voiceControl.first==Data([3]),"priority preserves voice control order")
        voiceControl.acknowledge()
        check(voiceControl.first==Data([1]),"background controls remain after the finite voice burst")
        var partialControl=BLEWriteBuffer()
        _=partialControl.append([Data([1]),Data([2])],radio:false)
        partialControl.acknowledge()
        _=partialControl.append([Data([3])],radio:false,voice:true)
        check(partialControl.first==Data([2]),"credit-based partial control remains atomic even without an ATT request in flight")
        var delayed=BLEWriteBuffer(),pacing=BLEWritePacing()
        _=delayed.append([Data([1])],radio:false)
        pacing.completed(.control,now:1)
        check(pacing.delay(.control,now:1.1)>0,"whole-packet pacing bounds a burst even when fragment credits remain")
        _=delayed.append([Data([2])],radio:false,repair:.init(id:100,deadline:2))
        check(delayed.first==Data([2]) && pacing.delay(.voice,now:1.1)==0,"repair bypasses an unstarted paced control packet")
        delayed.acknowledge();pacing.completed(.voice,now:1.1)
        check(delayed.first==Data([1]) && pacing.delay(.control,now:1.21)==0,"repair does not move the background service deadline")
        pacing.completed(.radio,now:2)
        check(pacing.delay(.control,now:2)==0 && pacing.delay(.radio,now:2.01)>0,"radio pacing does not hold ordinary control")
        var media=BLEWriteBuffer(),mediaBudget=BLEWritePacing()
        _=media.append([Data([1])],radio:false)
        _=media.append([Data([2])],radio:true)
        mediaBudget.completed(.control,now:1)
        media.selectReady{mediaBudget.delay($0,now:1.01)==0}
        check(media.first==Data([2]),"paced background control cannot hold an eligible radio packet")
        media.acknowledge()
        _=media.append([Data([3])],radio:true)
        mediaBudget.completed(.radio,now:1.02)
        media.selectReady{mediaBudget.delay($0,now:1.22)==0}
        check(media.first==Data([1]),"eligible ordinary control keeps FIFO progress beside radio")
        media.reset()
        _=media.append([Data([1]),Data([2])],radio:false);media.acknowledge()
        _=media.append([Data([3])],radio:true)
        media.selectReady{$0 == .radio}
        check(media.first==Data([2]),"pacing eligibility never preempts a partial assembly")
        pacing=BLEWritePacing()
        check(pacing.delay(.control,now:0)==0 && pacing.delay(.radio,now:0)==0,"a new connection has no inherited pacing deadline")
        var burst=BLEWriteBuffer(),budget=BLEWritePacing(),background:[Double]=[],urgent=0
        for _ in 0..<20 {_=burst.append([Data([0])],radio:false)}
        for tick in 0..<400 {
            let now=Double(tick)*0.02
            if tick%5==0 {
                _=burst.append([Data([1])],radio:false,repair:.init(id:UInt64(tick+1),deadline:now+0.3))
            }
            while let kind=burst.firstKind,budget.delay(kind,now:now)<0.000001 {
                switch kind {
                case .control:background.append(now)
                case .voice:urgent+=1
                case .radio:break
                }
                burst.acknowledge();budget.completed(kind,now:now)
            }
        }
        let intervals=zip(background,background.dropFirst()).map{$1-$0}
        check(background.count==20 && urgent==80 && burst.isEmpty,"continuous bounded repairs and a control burst both make progress")
        check(intervals.allSatisfy{$0>=BLEWritePacing.controlInterval-0.000001},"fragment credits cannot bypass whole-control production rate")
        for firstLeg in [ConnectionTransport.wifi,.bluetooth] {
            let other:ConnectionTransport=firstLeg == .wifi ? .bluetooth:.wifi
            var receipt=VoiceRepairReceipt(wifi:true,bluetooth:true)
            check(receipt.complete(firstLeg,.notSubmitted)==nil,"one failed mirror cannot refund a still pending request")
            check(receipt.complete(other,.submitted) == .submitted,"the surviving carrier commits exactly one logical request")
            check(receipt.complete(firstLeg,.notSubmitted)==nil,"late mirror expiry cannot undo successful submission")
            var unsent=VoiceRepairReceipt(wifi:true,bluetooth:true)
            _=unsent.complete(firstLeg,.notSubmitted)
            check(unsent.complete(other,.notSubmitted) == .notSubmitted,"only all-unsent mirrors refund the reservation")
        }
        let whileBlocked=queue.discardExpiredRepairs(now:5,writing:true)
        check(whileBlocked.repairs.isEmpty && queue.first==Data([9]),"expiry never alters a non-repair control write in flight")
        queue.reset()
        _=queue.append([Data([1]),Data([2])],radio:false,repair:.init(id:10,deadline:3))
        check(queue.acknowledge()==nil,"partial ATT completion does not submit a JSON request")
        check(queue.acknowledge() == .init(id:10,submission:.submitted),"last acknowledged fragment completes the logical request")
        check(queue.acknowledge()==nil && queue.reset().isEmpty,"completion is emitted only once")
        _=queue.append([Data([1]),Data([2])],radio:false,repair:.init(id:11,deadline:3))
        let partial=queue.reset(writing:true)
        check(partial==[.init(id:11,submission:.notSubmitted)],"disconnect before the last fragment leaves no complete request")
        _=queue.append([Data([1])],radio:false,repair:.init(id:12,deadline:3))
        check(queue.reset(writing:true)==[.init(id:12,submission:.uncertain)],"disconnect during the final ATT write preserves uncertainty")
        _=queue.append([Data([1]),Data([2])],radio:false,repair:.init(id:13,deadline:3))
        _=queue.append([Data([3])],radio:false)
        let cancelled=queue.discardVoiceRepair(writing:true)
        check(cancelled.repairs==[.init(id:13,submission:.notSubmitted)] && queue.count==2,"cancellation retains only the actual outstanding fragment")
        check(queue.acknowledge()==nil && queue.first==Data([3]),"late ATT callback cannot remove the following control packet")
    }
}
