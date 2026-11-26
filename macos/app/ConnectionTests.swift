import Foundation
import Darwin

enum ConnectionTests {
    static func run(_ check:(Bool,String)->Void) {
        TransportPolicyTests.run(check)
        var sleep=SleepResumePolicy()
        let beforeSleep=sleep.generation
        check(sleep.acceptsCompletion(from:beforeSleep),"permission work can complete within an awake session")
        check(!sleep.resume(.display),"ordinary display wake does not restart a running engine")
        sleep.willSleep()
        check(!sleep.acceptsCompletion(from:beforeSleep),"permission callbacks cannot relaunch the app during sleep")
        check(!sleep.resume(.display),"display wake cannot substitute for a system wake")
        check(!sleep.resume(.backgroundMaintenance) && sleep.waitingForWake,"background maintenance keeps device traffic suspended")
        check(sleep.didWake() && !sleep.waitingForWake,"a system wake resumes work without requiring any display")
        check(!sleep.acceptsCompletion(from:beforeSleep) && sleep.acceptsCompletion(from:sleep.generation),"old permission callbacks remain invalid after wake; new probes are permitted")
        check(!sleep.didWake() && !sleep.resume(.display),"duplicate system and screen wakes cannot create extra BLE managers")
        for _ in 0..<20 {
            sleep.willSleep()
            check(sleep.didWake(),"normal wake resumes across repeated sleep cycles")
        }
        sleep.willSleep()
        check(sleep.userRequestedResume() && !sleep.userRequestedResume(),"explicit user connection recovers a missing system notification only once")
        let lifecycle=EngineLifecycle();lifecycle.userPaused=true;lifecycle.suspend()
        let oldResume=lifecycle.requestResume();lifecycle.suspend()
        check(!lifecycle.completeResume(oldResume) && lifecycle.isSuspended,"late wake work cannot undo a more recent sleep")
        let newResume=lifecycle.requestResume()
        check(lifecycle.completeResume(newResume) && !lifecycle.isSuspended && lifecycle.userPaused,"wake keeps explicit disconnect intent")
        Engine.testSleepLifecycle(check)
        check(TransportPolicy.voiceStall<ConnectionTiming.voiceTimeout && ConnectionTiming.linkFresh<ConnectionTiming.sessionTimeout,"audio fallback precedes voice expiry; sustained link silence precedes session disconnect")
        check(ConnectionTiming.voiceTimeout<ConnectionTiming.bleSilenceTimeout && ConnectionTiming.bleSilenceTimeout<ConnectionTiming.sessionTimeout,"stalled BLE recovers after safe voice release and before logical session expiry")
        var writes=BLEWriteBuffer()
        check(writes.append([Data([1]),Data([2]),Data([3])],radio:true),"BLE media enqueued as complete fragments")
        check(writes.append([Data([4]),Data([5])],radio:false),"BLE control follows complete packet")
        writes.discardRadio(writing:true)
        check(writes.count==3 && writes.first==Data([1]),"cancellation preserves only active media fragment and control packets")
        writes.acknowledge();check(writes.first==Data([4]),"next control packet starts immediately after cancelled media")
        writes.reset();check(writes.append([Data](repeating:Data([0]),count:128),radio:true),"BLE bounded media budget")
        check(!writes.append([Data([0])],radio:true) && writes.append([Data](repeating:Data([0]),count:128),radio:false),"stalled radio retains control capacity")
        writes.discardRadio(writing:false);check(writes.count==128,"radio purge preserves every queued control fragment")
        writes.reset();let key=Data([5,1,2])
        check(writes.append([Data([1]),Data([2])],radio:true,key:key) && writes.append([Data([3])],radio:true,key:key) && writes.count==2,"BLE retry cannot queue another copy of an already pending radio packet")
        func radioKey(_ session:UInt64,_ sequence:UInt32)->Data {
            var bytes=Data([5]);for i in 0..<8 {bytes.append(UInt8(truncatingIfNeeded:session>>(i*8)))}
            for i in 0..<4 {bytes.append(UInt8(truncatingIfNeeded:sequence>>(i*8)))};return bytes
        }
        writes.reset()
        for sequence in 0..<10 {_=writes.append([Data([UInt8(sequence)]),Data([99])],radio:true,key:radioKey(1,UInt32(sequence)))}
        _=writes.append([Data([88])],radio:false)
        writes.acknowledgeRadio(session:1,next:2,mask:0b101,writing:true)
        check(writes.count==14 && writes.first==Data([0]),"mirrored radio receipts prune obsolete and received packets while preserving in-flight write and control")
        writes.acknowledge();writes.acknowledgeRadio(session:1,next:10,mask:0,writing:false)
        check(writes.count==1 && writes.first==Data([88]),"all acknowledged radio fragments leave control immediately available")
        writes.reset();_=writes.append([Data([1])],radio:true,key:radioKey(1,0))
        _=writes.append([Data([2])],radio:true,key:radioKey(2,0));writes.acknowledgeRadio(session:2,next:0,mask:0,writing:false)
        check(writes.count==1 && writes.first==Data([2]),"radio queue removes stale session without dropping new unreceived audio")
        writes.reset()
        check(writes.append([Data([10]),Data([11])],radio:false,repair:.init(id:1,deadline:0.2)),"one complete voice repair can be queued")
        for _ in 0..<100 {
            check(!writes.append([Data([12]),Data([13])],radio:false,repair:.init(id:2,deadline:0.3)),"congestion cannot accumulate more repair packets")
        }
        check(writes.count==2 && writes.append([Data([20]),Data([21])],radio:false),"repair backpressure preserves ordinary control capacity")
        writes.acknowledge()
        check(!writes.discardExpiredRepairs(now:0.199).headRemoved && writes.first==Data([11]),"unexpired repair keeps its remaining fragments intact")
        check(writes.discardExpiredRepairs(now:0.2).headRemoved && writes.first==Data([20]) && writes.count==2,"expired partial repair leaves next complete control packet available")
        writes.reset();_=writes.append([Data([10]),Data([11])],radio:false,repair:.init(id:1,deadline:0.2))
        _=writes.append([Data([20])],radio:false);writes.discardVoiceRepair(writing:true)
        check(writes.count==2 && writes.first==Data([10]),"recording cancellation preserves the actual outstanding ATT callback")
        writes.acknowledge();check(writes.first==Data([20]),"cancelled repair tail cannot cross into the next recording")
        _=writes.append([Data([12])],radio:false,repair:.init(id:2,deadline:0.3));writes.discardVoiceRepair(writing:false)
        check(writes.count==1 && writes.first==Data([20]),"idle recording cleanup keeps non-repair control untouched")
        let mediaParts=BLEFrame.fragments(Data(repeating:0,count:1218),identifier:1,limit:244,media:true)
        check(mediaParts.allSatisfy{$0[2]&4 != 0},"radio fragments have independent assembly channel")
        Engine.testConnections(check)
        let epoch=Date(),id="aabbccddeeff"
        var retry=BLEConnectionRetry(),attempt=epoch
        for delay in [TimeInterval(3),6,12,24,48,60,60,60] {
            check(retry.allows(attempt),"BLE retry becomes eligible at its deadline")
            retry.failed(attempt)
            check(!retry.allows(attempt.addingTimeInterval(delay-0.1)),"BLE advertisements cannot bypass failure backoff")
            check(retry.allows(attempt.addingTimeInterval(delay)),"BLE retries resume after bounded backoff")
            attempt=attempt.addingTimeInterval(delay)
        }
        check(retry.failures==6 && retry.allows(epoch.addingTimeInterval(-1)),"BLE retry count is bounded and clock rollback cannot strand reconnection")
        retry=BLEConnectionRetry();check(retry.allows(epoch),"explicit reconnect or successful business traffic clears BLE backoff")
        retry.requirePairingRepair()
        check(!retry.allows(epoch.addingTimeInterval(86400)) && !retry.allows(epoch.addingTimeInterval(-1)),"lost pairing records stop automatic reconnect despite elapsed time or clock rollback")
        let another=BLEConnectionRetry();check(another.allows(epoch),"pairing repair does not block another device")
        retry=BLEConnectionRetry();check(retry.allows(epoch),"explicit retry allows a new pairing attempt after OS repair")
        check(BluetoothLink.isPairingRecordLost(NSError(domain:"CBErrorDomain",code:14)),"recognize the OS peer-removed-pairing error")
        check(!BluetoothLink.isPairingRecordLost(NSError(domain:"CBErrorDomain",code:6)) && !BluetoothLink.isPairingRecordLost(NSError(domain:"Unrelated",code:14)) && !BluetoothLink.isPairingRecordLost(nil),"generic disconnects and unrelated errors do not require OS pairing repair")
        var from=sockaddr_in();from.sin_family=sa_family_t(AF_INET);from.sin_port=UInt16(33333).bigEndian;from.sin_addr.s_addr=inet_addr("192.168.1.10")
        var catalog=DeviceDiscovery()
        let first:[String:Any]=["event":"device.hello","device":id,"paired":true,"busy":true,"selection":true]
        check(catalog.accept(first,from:from,now:epoch)?.paired==true,"discover foreign-bound device without selecting or authenticating")
        let peripheral=UUID();catalog.foundBLE(id,identifier:peripheral,connectable:true,now:epoch)
        check(catalog.visible(now:epoch).count==1 && catalog.devices[id]?.bleID==peripheral,"merge LAN and BLE by physical device identity")
        _=catalog.accept(first,from:from,now:epoch.addingTimeInterval(1))
        check(catalog.devices[id]?.bleID==peripheral,"LAN update retains BLE identity")
        _=catalog.acceptBLE(["event":"device.hello","device":id,"paired":false,"busy":false],id:id,now:epoch.addingTimeInterval(2))
        check(catalog.devices[id]?.endpoint.sin_addr.s_addr==from.sin_addr.s_addr,"BLE update retains LAN endpoint")
        check(catalog.acceptBLE(["event":"config.reply","device":id],id:id)==nil,"business replies cannot overwrite discovery metadata")
        check(catalog.accept(["event":"device.hello","device":"not-a-device"],from:from)==nil,"ignore malformed identities")
        check(catalog.visible(now:epoch.addingTimeInterval(40)).isEmpty,"expire absent device rows")
        for length in [1,2,17,804,1218,1537] {
            let source=Data((0..<length).map{UInt8($0&255)})
            for mtu in [20,64,182,244,512] {
                let pieces=BLEFrame.fragments(source,identifier:255,limit:mtu)
                var decoder=BLEFrame(),answer:Data?
                for (index,piece) in pieces.enumerated() {
                    let result=decoder.accept(piece,now:epoch)
                    if index<pieces.count-1 { check(result==nil,"BLE fragment cannot dispatch partial packet") }
                    answer=result
                }
                check(answer==source,"BLE framing round trip \(length)/\(mtu)")
            }
        }
        let pieces=BLEFrame.fragments(Data(repeating:7,count:40),identifier:1,limit:20)
        var decoder=BLEFrame();_=decoder.accept(pieces[0],now:epoch)
        check(decoder.accept(pieces[1],now:epoch.addingTimeInterval(3))==nil,"expire stalled BLE frame")
        decoder=BLEFrame();_=decoder.accept(pieces[0],now:epoch)
        check(decoder.accept(pieces[2],now:epoch)==nil,"reject missing BLE fragment")
        check(BLEFrame.fragments(Data(repeating:0,count:1538),identifier:0,limit:244).isEmpty,"bound outgoing BLE packet")
        print("Connection discovery and BLE frame tests: PASS")
    }
}
