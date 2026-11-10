import Foundation
import Darwin

// Loopback-only, OS-assigned port, account-private endpoint file, rotated token.
// The same universal executable runs a tiny hook helper without starting AppKit.
final class AgentReceiver {
    private let work=DispatchQueue(label:"vibe.agent.ipc")
    private var source:DispatchSourceRead?
    private var nonce=""
    static var endpoint:URL {
        if let test=ProcessInfo.processInfo.environment["VIBE_AGENT_ENDPOINT"] { return URL(fileURLWithPath:test) }
        return PairingStore.directory.appendingPathComponent("agent-endpoint.json")
    }
    private let endpointFile:URL
    init(endpoint:URL=AgentReceiver.endpoint) { endpointFile=endpoint }
    func start(_ callback:@escaping(AgentEvent)->Void)throws {
        let fd=Darwin.socket(AF_INET,SOCK_DGRAM,0)
        guard fd>=0 else { throw failure() }
        var address=sockaddr_in();address.sin_len=UInt8(MemoryLayout<sockaddr_in>.size);address.sin_family=sa_family_t(AF_INET);address.sin_addr.s_addr=inet_addr("127.0.0.1")
        let bound=withUnsafePointer(to:&address){$0.withMemoryRebound(to:sockaddr.self,capacity:1){Darwin.bind(fd,$0,socklen_t(MemoryLayout<sockaddr_in>.size))}}
        guard bound==0 else { Darwin.close(fd);throw failure() }
        var length=socklen_t(MemoryLayout<sockaddr_in>.size)
        guard withUnsafeMutablePointer(to:&address,{$0.withMemoryRebound(to:sockaddr.self,capacity:1){getsockname(fd,$0,&length)}})==0 else { Darwin.close(fd);throw failure() }
        _=fcntl(fd,F_SETFL,O_NONBLOCK)
        nonce=PairingStore.random()
        do {
            try AgentInstaller.privateWrite(try JSONSerialization.data(withJSONObject:["port":Int(UInt16(bigEndian:address.sin_port)),"token":nonce]),to:endpointFile,backup:false)
        } catch { Darwin.close(fd);throw error }
        let token=nonce
        let read=DispatchSource.makeReadSource(fileDescriptor:fd,queue:work)
        read.setEventHandler {
            var bytes=[UInt8](repeating:0,count:4097)
            for _ in 0..<32 {
                let count=Darwin.recv(fd,&bytes,bytes.count,0);if count<=0 { break }
                guard count<=4096,let body=try? JSONSerialization.jsonObject(with:Data(bytes.prefix(count))) as? [String:Any],body["token"] as? String==token,
                      let object=body["event"],let data=try? JSONSerialization.data(withJSONObject:object),let event=try? JSONDecoder().decode(AgentEvent.self,from:data) else { continue }
                DispatchQueue.main.async { callback(event) }
            }
        }
        read.setCancelHandler{Darwin.close(fd)};source=read;read.resume()
    }
    private func failure()->NSError { NSError(domain:"VibeAgent",code:Int(errno),userInfo:[NSLocalizedDescriptionKey:"本地通知端口无法启动"]) }
    deinit {
        source?.cancel()
        if let data=try? Data(contentsOf:endpointFile),let body=try? JSONSerialization.jsonObject(with:data) as? [String:Any],body["token"] as? String==nonce { try? FileManager.default.removeItem(at:endpointFile) }
    }
    static func forward(_ event:AgentEvent,endpoint:URL=endpoint)->Bool {
        guard let data=try? Data(contentsOf:endpoint),let body=try? JSONSerialization.jsonObject(with:data) as? [String:Any],let port=body["port"] as? Int,(1...65535).contains(port),let token=body["token"] as? String,token.count==64,
              let object=try? JSONSerialization.jsonObject(with:JSONEncoder().encode(event)),let packet=try? JSONSerialization.data(withJSONObject:["token":token,"event":object]),packet.count<=4096 else { return false }
        let fd=Darwin.socket(AF_INET,SOCK_DGRAM,0);guard fd>=0 else { return false };defer{Darwin.close(fd)}
        var address=sockaddr_in();address.sin_len=UInt8(MemoryLayout<sockaddr_in>.size);address.sin_family=sa_family_t(AF_INET);address.sin_addr.s_addr=inet_addr("127.0.0.1");address.sin_port=UInt16(port).bigEndian
        return packet.withUnsafeBytes { bytes in withUnsafePointer(to:&address){$0.withMemoryRebound(to:sockaddr.self,capacity:1){sendto(fd,bytes.baseAddress,bytes.count,0,$0,socklen_t(MemoryLayout<sockaddr_in>.size))}} }==packet.count
    }
    static func runHook(_ args:[String]) {
        defer { print("{}") } // No decisions, approvals or context injected into the Agent.
        guard args.count>=1,let agent=AgentID(rawValue:args[0]) else { return }
        // Poll caps broken stdin producers; no hook can wait indefinitely for EOF.
        var data=Data(),buffer=[UInt8](repeating:0,count:8192)
        let deadline=Date().addingTimeInterval(0.8)
        while Date()<deadline && data.count<=262144 {
            var descriptor=pollfd(fd:STDIN_FILENO,events:Int16(POLLIN),revents:0)
            let ready=poll(&descriptor,1,50)
            if ready<0 { return };if ready==0 { continue }
            let count=Darwin.read(STDIN_FILENO,&buffer,buffer.count)
            if count==0 { break };if count<0 { return };data.append(contentsOf:buffer.prefix(count))
        }
        guard data.count<=262144,let payload=try? JSONSerialization.jsonObject(with:data) as? [String:Any],let event=AgentEvent.parse(agent:agent,event:args.count>1 ? args[1] : "",payload:payload) else { return }
        _=forward(event)
    }
}
