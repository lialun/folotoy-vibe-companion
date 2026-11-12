import Foundation
import CoreFoundation

enum ConnectionRevision {
    static func counter(_ value:Any?)->Int? {
        guard let n=value as? NSNumber,CFGetTypeID(n) != CFBooleanGetTypeID(),n.doubleValue.isFinite,
              n.doubleValue>=0,n.doubleValue<=Double(UInt32.max),n.doubleValue.rounded()==n.doubleValue else { return nil }
        return Int(n.uint32Value)
    }
    static func validHello(_ body:[String:Any],requireCurrentProtocol:Bool)->Bool {
        guard let proto=counter(body["proto"]),proto==2 || proto==3 else { return false }
        if proto==2 { return !requireCurrentProtocol }
        return counter(body["boot"]) != nil && counter(body["lease"]) != nil
    }
    static func matchesEvent(_ body:[String:Any],boot:Int?,lease:Int?)->Bool {
        guard let boot=boot,let lease=lease else { return false }
        return counter(body["boot"])==boot && counter(body["lease"])==lease
    }
}

enum WirelessConfirmation {
    /* A radio switch may remove the route carrying its acknowledgement. A
     * fresh, authenticated owner hello on the surviving route is authoritative. */
    static func matches(request:[String:Any],hello:[String:Any])->Bool {
        guard hello["paired"] as? Bool==true,hello["owned"] as? Bool==true,
              let wifi=request["wifiEnabled"] as? Bool,let ble=request["bleEnabled"] as? Bool,
              let actualWifi=hello["wifiEnabled"] as? Bool,let actualBLE=hello["bleEnabled"] as? Bool else { return false }
        return wifi==actualWifi && ble==actualBLE
    }
}
/* A delayed hello must not restore a revoked owner or an obsolete endpoint.
 * Boot nonces are opaque; leases are monotonic 32-bit serial numbers per boot. */
struct RevisionFence {
    enum Acceptance { case stale,initial,current,restarted }
    private(set) var boot:Int?,lease:Int?
    private var retired:[Int]=[]
    mutating func accept(boot nextBoot:Int?,lease nextLease:Int?)->Acceptance {
        if let nextBoot=nextBoot,retired.contains(nextBoot) { return .stale }
        if nextBoot==boot,let next=nextLease,let old=lease,next != old,
           UInt32(truncatingIfNeeded:next) &- UInt32(truncatingIfNeeded:old)>=0x80000000 { return .stale }
        let result:Acceptance
        if let previous=boot,nextBoot != boot {
            retired.append(previous);if retired.count>4 { retired.removeFirst() };result = .restarted
        } else { result = boot==nil ? .initial : .current }
        boot=nextBoot;lease=nextLease;return result
    }
}
