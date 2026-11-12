import Foundation
enum BLETests {
    static func run(_ check:(Bool,String)->Void) {
        var discovery=BLEDiscoveryPolicy()
        check(discovery.permitsConnection,"BLE initially permits discovery")
        discovery.reportDevice(enabled:false)
        check(!discovery.permitsConnection,"device-disabled BLE stops automatic discovery and reconnects")
        discovery.retry()
        check(discovery.permitsConnection,"manual refresh can find a locally re-enabled BLE-only device")
        discovery.reportDevice(enabled:false);discovery.reportDevice(enabled:true)
        check(discovery.permitsConnection,"fresh enabled status resumes discovery")
        discovery.park();discovery.reportDevice(enabled:false);discovery.reportDevice(enabled:true)
        check(!discovery.permitsConnection,"radio state updates cannot take over another computer's BLE slot")
        discovery.retry()
        check(discovery.permitsConnection,"explicit takeover resumes a parked BLE connection")
        let requested:[String:Any]=["wifiEnabled":false,"bleEnabled":true]
        check(WirelessConfirmation.matches(request:requested,hello:["paired":true,"owned":true,"wifiEnabled":false,"bleEnabled":true]),"surviving owner route confirms applied radio switch")
        check(!WirelessConfirmation.matches(request:requested,hello:["paired":true,"owned":false,"wifiEnabled":false,"bleEnabled":true]),"another owner cannot confirm our radio switch")
        check(!WirelessConfirmation.matches(request:requested,hello:["paired":true,"owned":true,"wifiEnabled":true,"bleEnabled":true]),"old radio state cannot acknowledge a pending switch")
        check(!WirelessConfirmation.matches(request:requested,hello:["paired":true,"owned":true]),"missing radio flags cannot acknowledge a switch")
        var fence=RevisionFence()
        let hello:[String:Any]=["proto":3,"boot":10,"lease":2]
        check(ConnectionRevision.validHello(hello,requireCurrentProtocol:true),"valid current protocol revision")
        for bad:Any in [true,-1,0.5,Double(UInt32.max)+1,"10"] {
            check(!ConnectionRevision.validHello(["proto":3,"boot":bad,"lease":2],requireCurrentProtocol:false),"reject malformed boot identity")
            check(!ConnectionRevision.validHello(["proto":3,"boot":10,"lease":bad],requireCurrentProtocol:false),"reject malformed lease")
        }
        check(!ConnectionRevision.validHello(["proto":3,"boot":10],requireCurrentProtocol:true),"missing lease cannot erase the revision fence")
        check(!ConnectionRevision.validHello(["proto":2],requireCurrentProtocol:true),"current connection cannot downgrade to an unfenced legacy hello")
        check(ConnectionRevision.validHello(["proto":2],requireCurrentProtocol:false),"initial legacy connection remains supported")
        check(ConnectionRevision.matchesEvent(["boot":10,"lease":2],boot:10,lease:2),"event matches owner and boot")
        check(!ConnectionRevision.matchesEvent(["lease":2],boot:10,lease:2),"current protocol events cannot omit the boot identity")
        check(!ConnectionRevision.matchesEvent(["boot":9,"lease":2],boot:10,lease:2),"previous-boot event cannot match a reused lease")
        check(!ConnectionRevision.matchesEvent(["boot":10,"lease":1],boot:10,lease:2),"old owner event cannot cross takeover")
        check(fence.accept(boot:10,lease:1) == .initial,"initial device revision")
        check(fence.accept(boot:10,lease:3) == .current,"accept new owner generation")
        check(fence.accept(boot:10,lease:2) == .stale && fence.lease==3,"delayed status cannot restore previous owner")
        check(fence.accept(boot:11,lease:1) == .restarted,"device reboot resets lease serial")
        check(fence.accept(boot:10,lease:4) == .stale,"old boot cannot return after reconnect")
        check(fence.accept(boot:11,lease:0xFFFFFFFE) == .stale,"reject backwards serial")
        var wrapping=RevisionFence();_ = wrapping.accept(boot:1,lease:0xFFFFFFFF)
        check(wrapping.accept(boot:1,lease:0) == .current,"lease serial handles wrap")
        let packet=Data((0..<1537).map { UInt8(truncatingIfNeeded:$0) })
        for capacity in [20,182,244] {
            var receiver=BLEAssembly(),result:Data?
            let frames=BLEWire.frames(packet,id:65535,capacity:capacity)
            for frame in frames { result=receiver.append(frame) }
            check(result==packet,"BLE MTU \(capacity+3) complete bounded reassembly")
            check(frames.allSatisfy{$0.count<=capacity},"BLE respects negotiated write length")
            _=receiver.append(frames[0]);check(receiver.append(frames.last!)==nil,"BLE rejects missing or reordered chunks")
            _=receiver.append(frames[0],now:Date(timeIntervalSince1970:1))
            check(receiver.append(frames[1],now:Date(timeIntervalSince1970:4))==nil,"BLE expires incomplete messages")
        }
        check(BLEWire.frames(Data(repeating:0,count:1538),id:0,capacity:244).isEmpty,"BLE caps untrusted payload")
        check(BLEWire.frames(packet,id:0,capacity:6).isEmpty,"BLE rejects unusable negotiated MTU")
        let directory=FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at:directory) }
        do {
            let first=try PairingStore.clientID(directory:directory),second=try PairingStore.clientID(directory:directory)
            check(first==second && first.count==32,"persistent computer identity across routes and launches")
            check(PairingStore.computerName(String(repeating:"电脑",count:50)).utf8.count<=63,"computer name stays complete UTF-8 within device bounds")
            check(PairingStore.computerName("\n")=="Computer","computer names exclude control characters")
        } catch { check(false,"computer identity persistence: \(error)") }
    }
}
