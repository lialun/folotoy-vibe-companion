import Foundation

enum TransportPolicyTests {
    static func run(_ check:(Bool,String)->Void) {
        let t=Date(timeIntervalSince1970:1000)
        var policy=TransportPolicy()
        func decide(_ current:ConnectionTransport,_ wifi:TimeInterval,_ ble:TimeInterval,_ at:TimeInterval,
                    _ voice:TimeInterval?=nil,_ audio:TimeInterval=0)->TransportPolicy.Change? {
            policy.select(current:current,wifi:t.addingTimeInterval(wifi),ble:t.addingTimeInterval(ble),
                          now:t.addingTimeInterval(at),voiceStarted:voice.map{t.addingTimeInterval($0)},audio:t.addingTimeInterval(audio))
        }
        for i in 1...60 {
            let now=Double(i)
            check(decide(.bluetooth,now,now,now)==nil,"restored Wi-Fi cannot steal a healthy BLE session")
            check(decide(.wifi,now-5.9,now,now)==nil,"heartbeat jitter below six seconds does not switch links")
            check(decide(.wifi,now,now,now,now-3,now-2.9)==nil,"sub-three-second audio gaps do not switch links")
        }
        check(decide(.wifi,0,6,6)?.transport == .bluetooth,"six seconds of total silence permits a fresh backup")
        check(decide(.bluetooth,6.1,6.1,6.1)==nil,"one recovered Wi-Fi heartbeat cannot reverse failover")
        check(decide(.bluetooth,7,7,9.2,6,6)==nil,"soft stalls cannot ping-pong within the fifteen-second hold")
        check(decide(.bluetooth,13,6,13)?.transport == .wifi,"a failed new link can escape hold after actual loss")
        policy.reset()
        check(decide(.wifi,5,5,5,0,2)?.reason=="audio-stalled","heartbeats cannot mask a three-second audio outage")
        check(decide(.bluetooth,5.1,5.1,5.1,0,2)==nil,"late Wi-Fi traffic cannot undo a voice failover")
        policy.reset()
        check(decide(.wifi,0,0,8,0,0)==nil,"never fail over to an equally stale backup")
        check(decide(.wifi,0,5,8,0,0)==nil,"backup requires traffic newer than three seconds")
        check(decide(.wifi,8,8,8,7,0)==nil,"a new recording gets its own startup grace")
        check(policy.select(current:.wifi,wifi:.distantPast,ble:t,now:t,voiceStarted:nil,audio:.distantPast)?.transport == .bluetooth,"a missing attachment uses the available authorized link immediately")
        policy.reset()
        check(policy.select(current:.wifi,wifi:t,ble:t,now:t,voiceStarted:t.addingTimeInterval(-10),audio:t,severeLoss:true)?.reason=="audio-loss","sustained severe loss can fail over even if occasional audio still arrives")
        var loss=VoiceLossWindow();loss.reset(now:t)
        for _ in 0..<15 {loss.record(missing:0)}
        loss.record(missing:34) // 16 received + 34 missing = 68%.
        check(!loss.severe(now:t.addingTimeInterval(5)),"ordinary loss below the severe threshold does not select another route")
        loss.reset(now:t)
        for _ in 0..<15 {loss.record(missing:0)}
        loss.record(missing:40)
        check(!loss.severe(now:t.addingTimeInterval(4.9)) && loss.severe(now:t.addingTimeInterval(5)),"severe loss requires a full five-second window")
        for _ in 0..<50 {loss.record(missing:0)}
        check(!loss.severe(now:t.addingTimeInterval(10)),"a healthy window clears the degraded observation")
        loss.reset(now:t);loss.record(missing:3)
        check(!loss.severe(now:t.addingTimeInterval(5)),"sparse startup samples cannot trigger quality failover")

        var retry=BLEWriteRetry()
        for delay in [0.1,0.25,0.5,1.0] {check(retry.delay()==delay,"ATT congestion uses bounded increasing delays")}
        check(retry.delay()==nil && retry.attempts==4,"repeated ATT congestion cannot retry forever")
        retry.reset();check(retry.delay()==0.1,"successful fragment or new connection resets write backoff")
        for code in [9,17] {
            let error=NSError(domain:"CBATTErrorDomain",code:code)
            check(BluetoothLink.isWriteCongestion(error) && !BluetoothLink.isWriteAuthenticationError(error),"resource/queue pressure is not a pairing failure")
        }
        for code in [5,8,15] {
            check(BluetoothLink.isWriteAuthenticationError(NSError(domain:"CBATTErrorDomain",code:code)),"actual ATT security failures retain their repair path")
        }
        check(!BluetoothLink.isWriteCongestion(NSError(domain:"CBErrorDomain",code:17)),"congestion classification also checks the error domain")
        print("Stable transport and bounded ATT backpressure: PASS")
    }
}
