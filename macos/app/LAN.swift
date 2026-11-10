import Foundation
import Darwin

enum LANError:LocalizedError {
    case system(Int32)
    var errorDescription:String? { if case .system(let code)=self { return "无法打开局域网连接（\(code)）：\(String(cString:strerror(code)))" };return nil }
}

// One socket handles discovery, control and audio. Port zero asks the OS for
// an exclusive available port; only the device's discovery port is fixed.
struct LANSocket {
    static let discoveryPort:UInt16=33333
    static let negotiatedHeartbeatSeconds=10
    static func heartbeat(capable:Bool)->TimeInterval { capable ? TimeInterval(negotiatedHeartbeatSeconds) : 5 }
    static func peerTimeout(capable:Bool)->TimeInterval { heartbeat(capable:capable)*3 }
    let descriptor:Int32,port:UInt16
    static func open(port:UInt16=0) throws -> LANSocket {
        let fd=Darwin.socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP)
        guard fd>=0 else { throw LANError.system(errno) }
        var complete=false
        defer { if !complete { Darwin.close(fd) } }
        var address=sockaddr_in();address.sin_len=UInt8(MemoryLayout<sockaddr_in>.size)
        address.sin_family=sa_family_t(AF_INET);address.sin_port=port.bigEndian
        let bound=withUnsafePointer(to:&address) { p in p.withMemoryRebound(to:sockaddr.self,capacity:1) { Darwin.bind(fd,$0,socklen_t(MemoryLayout<sockaddr_in>.size)) } }
        guard bound==0 else { throw LANError.system(errno) }
        var size=socklen_t(MemoryLayout<sockaddr_in>.size)
        let named=withUnsafeMutablePointer(to:&address) { p in p.withMemoryRebound(to:sockaddr.self,capacity:1) { getsockname(fd,$0,&size) } }
        guard named==0,address.sin_port != 0 else { throw LANError.system(named==0 ? EINVAL : errno) }
        var yes:Int32=1
        guard setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&yes,socklen_t(MemoryLayout<Int32>.size))==0,
              fcntl(fd,F_SETFL,O_NONBLOCK)==0 else { throw LANError.system(errno) }
        complete=true;return LANSocket(descriptor:fd,port:UInt16(bigEndian:address.sin_port))
    }
    static func sameEndpoint(_ a:sockaddr_in,_ b:sockaddr_in)->Bool {
        a.sin_family==sa_family_t(AF_INET) && b.sin_family==sa_family_t(AF_INET) &&
        a.sin_port != 0 && a.sin_port==b.sin_port && a.sin_addr.s_addr==b.sin_addr.s_addr
    }
    @discardableResult static func send(_ data:Data,from fd:Int32,to endpoint:sockaddr_in)->Bool {
        var destination=endpoint
        guard fd>=0,destination.sin_port != 0,destination.sin_addr.s_addr != 0 else { return false }
        return data.withUnsafeBytes { bytes in withUnsafePointer(to:&destination) { p in p.withMemoryRebound(to:sockaddr.self,capacity:1) { Darwin.sendto(fd,bytes.baseAddress,bytes.count,0,$0,socklen_t(MemoryLayout<sockaddr_in>.size)) } } }==data.count
    }
    static func beacon(port:UInt16,token:String,audioReady:Bool,hotkeyReady:Bool,epoch:Int,tz:Int)->[String:Any] {
        var body:[String:Any]=["v":2,"role":"pc","port":Int(port)]
        if !token.isEmpty {
            body.merge(["type":"pc.status","token":token,"audioReady":audioReady,"hotkeyReady":hotkeyReady,"epoch":epoch,"tz":tz,"idleHeartbeat":negotiatedHeartbeatSeconds]) { _,new in new }
        }
        return body
    }
}
