import Foundation
import Darwin

enum LANTests {
    static func run(_ check:(Bool,String)->Void) {
        check(LANSocket.heartbeat(capable:true)==10 && LANSocket.peerTimeout(capable:true)==30,"negotiated ten-second heartbeat and thirty-second loss tolerance")
        check(LANSocket.heartbeat(capable:false)==5 && LANSocket.peerTimeout(capable:false)==15,"legacy peer timing remains compatible")
        PowerDiagnosticsTests.run(check)
        do {
            // A running released client may already own it. Otherwise hold it
            // ourselves: production dynamic binding must work in either case.
            var blocker:LANSocket?
            do { blocker=try LANSocket.open(port:LANSocket.discoveryPort) }
            catch LANError.system(let code) { guard code==EADDRINUSE else { throw LANError.system(code) } }
            defer { if let blocker=blocker { Darwin.close(blocker.descriptor) } }
            let client=try LANSocket.open(),server=try LANSocket.open()
            defer { Darwin.close(client.descriptor);Darwin.close(server.descriptor) }
            check(client.port != LANSocket.discoveryPort && client.port != server.port,"automatic UDP port while 33333 is occupied")
            func loopback(_ port:UInt16)->sockaddr_in {
                var a=sockaddr_in();a.sin_len=UInt8(MemoryLayout<sockaddr_in>.size);a.sin_family=sa_family_t(AF_INET)
                a.sin_addr.s_addr=inet_addr("127.0.0.1");a.sin_port=port.bigEndian;return a
            }
            func receive(_ fd:Int32) throws -> (Data,sockaddr_in) {
                var ready=pollfd(fd:fd,events:Int16(POLLIN),revents:0)
                guard Darwin.poll(&ready,1,1000)>0 else { throw LANError.system(ETIMEDOUT) }
                var bytes=[UInt8](repeating:0,count:2048),from=sockaddr_in(),size=socklen_t(MemoryLayout<sockaddr_in>.size)
                let n=withUnsafeMutablePointer(to:&from) { p in p.withMemoryRebound(to:sockaddr.self,capacity:1) { Darwin.recvfrom(fd,&bytes,bytes.count,0,$0,&size) } }
                guard n>0 else { throw LANError.system(errno) };return (Data(bytes.prefix(n)),from)
            }
            let body=LANSocket.beacon(port:client.port,token:String(repeating:"a",count:64),audioReady:true,hotkeyReady:false,epoch:1800000000,tz:480)
            check(body["idleHeartbeat"] as? Int==10,"wire interval agrees with local cadence and installed firmware negotiation")
            check(body["port"] as? Int==Int(client.port) && body["type"] as? String=="pc.status" && body["audioReady"] as? Bool==true && body["hotkeyReady"] as? Bool==false,"one beacon carries readiness and actual client port")
            let unpaired=LANSocket.beacon(port:client.port,token:"",audioReady:true,hotkeyReady:true,epoch:1800000000,tz:480)
            check(unpaired["token"]==nil && unpaired["type"]==nil,"unpaired discovery cannot publish authenticated status")
            var beacon=Data([3]);beacon.append(try JSONSerialization.data(withJSONObject:body))
            check(LANSocket.send(beacon,from:client.descriptor,to:loopback(server.port)),"send discovery from selected socket")
            let (received,from)=try receive(server.descriptor)
            check(received==beacon && LANSocket.sameEndpoint(from,loopback(client.port)),"discovery actual source agrees with advertised port")
            let reply=Data([4])
            check(LANSocket.send(reply,from:server.descriptor,to:from),"device replies to source endpoint")
            let (answer,sender)=try receive(client.descriptor)
            check(answer==reply && LANSocket.sameEndpoint(sender,loopback(server.port)),"control and audio socket receives unicast replies")
            let other=try LANSocket.open();defer { Darwin.close(other.descriptor) }
            check(LANSocket.send(reply,from:other.descriptor,to:from),"inject same-IP wrong-port packet")
            let (_,wrong)=try receive(client.descriptor)
            check(!LANSocket.sameEndpoint(wrong,loopback(server.port)),"reject same IP from unselected port")
            let replacement=try LANSocket.open();defer { Darwin.close(replacement.descriptor) }
            check(LANSocket.send(beacon,from:replacement.descriptor,to:loopback(server.port)),"replacement client sends from new port")
            let (_,rebound)=try receive(server.descriptor)
            check(!LANSocket.sameEndpoint(rebound,from) && LANSocket.sameEndpoint(rebound,loopback(replacement.port)),"restarted client has distinct endpoint")
            check(LANSocket.send(reply,from:server.descriptor,to:rebound),"reply follows replacement endpoint")
            let (replacementReply,_)=try receive(replacement.descriptor)
            check(replacementReply==reply,"replacement receives reply without fixed client port")
            print("LAN sockets: occupied port, actual source, bidirectional traffic and endpoint replacement PASS")
        } catch { check(false,"LAN socket integration: \(error.localizedDescription)") }
    }
}
