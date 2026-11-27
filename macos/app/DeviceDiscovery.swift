import Foundation
import Darwin

enum ConnectionTiming {
    static let linkFresh:TimeInterval=TransportPolicy.silence,sessionTimeout:TimeInterval=9,voiceTimeout:TimeInterval=VoicePolicy.audioTimeout
    static let bleSilenceTimeout:TimeInterval=6
}
struct BLEConnectionRetry {
    private(set) var failures=0
    private(set) var pairingRepairRequired=false
    private(set) var nextAttempt=Date.distantPast
    private var lastFailure=Date.distantPast
    func allows(_ now:Date)->Bool { !pairingRepairRequired && (now>=nextAttempt || now<lastFailure) }
    mutating func requirePairingRepair() { pairingRepairRequired=true }
    mutating func waitForOwner(until:Date) {nextAttempt=max(nextAttempt,until)}
    mutating func failed(_ now:Date) {
        let delays:[TimeInterval]=[3,6,12,24,48,60]
        failures=min(failures+1,delays.count);lastFailure=now
        nextAttempt=now.addingTimeInterval(delays[failures-1])
    }
}

enum ConnectionTransport:String,CaseIterable { case wifi,bluetooth
    var title:String { self == .wifi ? "Wi-Fi" : "蓝牙" }
}
struct DiscoveredDevice:Identifiable {
    let id:String
    var endpoint:sockaddr_in
    var paired:Bool,busy:Bool,selection:Bool
    var metadataKnown=true
    var seen:Date
    var bleID:UUID?=nil,bleSeen=Date.distantPast,wifiSeen=Date.distantPast
    var hasWiFi:Bool { Date().timeIntervalSince(wifiSeen)<35 }
    var hasBLE:Bool { bleID != nil && Date().timeIntervalSince(bleSeen)<35 }
    var address:String {
        var ip=endpoint.sin_addr,buffer=[CChar](repeating:0,count:Int(INET_ADDRSTRLEN))
        inet_ntop(AF_INET,&ip,&buffer,socklen_t(buffer.count));return String(cString:buffer)
    }
    var title:String { "FoloToy · "+id.suffix(4).uppercased() }
}
struct DeviceDiscovery {
    private(set) var devices:[String:DiscoveredDevice]=[:]
    mutating func accept(_ body:[String:Any],from:sockaddr_in,now:Date=Date())->DiscoveredDevice? {
        guard body["event"] as? String=="device.hello",let id=body["device"] as? String,
              id.count==12,id.allSatisfy({ "0123456789abcdef".contains($0) }) else { return nil }
        devices=devices.filter { now.timeIntervalSince($0.value.seen)<35 }
        guard devices[id] != nil || devices.count<16 else { return nil }
        var device=DiscoveredDevice(id:id,endpoint:from,paired:body["paired"] as? Bool ?? false,
            busy:body["busy"] as? Bool ?? false,selection:body["selection"] as? Bool ?? false,seen:now)
        if let previous=devices[id] { device.bleID=previous.bleID;device.bleSeen=previous.bleSeen;device.wifiSeen=previous.wifiSeen }
        device.wifiSeen=now;devices[id]=device;return device
    }
    mutating func foundBLE(_ id:String,identifier:UUID,connectable:Bool,now:Date=Date()) {
        guard id.count==12,id.allSatisfy({ "0123456789abcdef".contains($0) }),devices[id] != nil || devices.count<16 else { return }
        let known=devices[id] != nil
        var entry=devices[id] ?? DiscoveredDevice(id:id,endpoint:sockaddr_in(),paired:false,busy:!connectable,selection:true,seen:now)
        if !known { entry.metadataKnown=false }
        entry.bleID=identifier;entry.bleSeen=now;entry.seen=now
        if !entry.hasWiFi { entry.busy = !connectable }
        devices[id]=entry
    }
    mutating func acceptBLE(_ body:[String:Any],id:String,now:Date=Date())->DiscoveredDevice? {
        guard body["event"] as? String=="device.hello",body["device"] as? String==id,var entry=devices[id] else { return nil }
        entry.metadataKnown=true;entry.paired=body["paired"] as? Bool ?? entry.paired;entry.busy=body["busy"] as? Bool ?? entry.busy
        entry.bleSeen=now;entry.seen=now;devices[id]=entry;return entry
    }
    mutating func visible(now:Date=Date())->[DiscoveredDevice] {
        devices=devices.filter { now.timeIntervalSince($0.value.seen)<35 }
        return devices.values.sorted { $0.id<$1.id }
    }
}

enum ComputerIdentity {
    static var displayName:String {
        let source=Host.current().localizedName ?? "Mac"
        let latin=source.applyingTransform(.toLatin,reverse:false)?.applyingTransform(.stripDiacritics,reverse:false) ?? source
        let clean=String(latin.unicodeScalars.filter { $0.value>=32 && $0.value<=126 }.prefix(48)).trimmingCharacters(in:.whitespaces)
        return clean.isEmpty ? "Mac-"+load().suffix(4).uppercased() : clean
    }
    static func load()->String {
        if let value=UserDefaults.standard.string(forKey:"computerIdentity"),UUID(uuidString:value) != nil { return value.lowercased() }
        let value=UUID().uuidString.lowercased();UserDefaults.standard.set(value,forKey:"computerIdentity");return value
    }
}
