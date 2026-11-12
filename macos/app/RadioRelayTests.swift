import Foundation
enum RadioRelayTests {
    static func probeError(_ address:String) {
        let queue=DispatchQueue(label:"vibe.relay.error-probe"),done=DispatchSemaphore(value:0)
        let relay=RadioRelay(queue:queue);var record:DiagnosticRecord?
        queue.async {
            relay.onFailure={ _,failure in record=failure;done.signal() }
            relay.send={ _ in true }
            relay.open(["stream":UInt32(777),"url":address,"rate":24000,"samples":1200,"next":UInt32(0),"limit":UInt32(26)])
        }
        _=done.wait(timeout:.now()+3)
        queue.sync { relay.stop() }
        guard let record=record,record.code == .sourceHTTP,record.numbers["http"]==404,
              record.valid,!String(decoding:try! JSONEncoder().encode(record),as:UTF8.self).contains("secret-value") else {
            fputs("Radio error probe: FAIL\n",stderr);exit(1)
        }
        print("Radio error probe: real decoder HTTP 404 -> typed private diagnostic PASS")
    }
    static func probe(_ address:String) {
        let queue=DispatchQueue(label:"vibe.relay.probe"),done=DispatchSemaphore(value:0)
        let relay=RadioRelay(queue:queue)
        var expected:UInt32=0,finished=false,passed=false
        queue.async {
            relay.onFailure={ _,record in if !finished { fputs("Radio probe failure: \(record.code.rawValue)\n",stderr);finished=true;done.signal() } }
            relay.send={ packet in
                guard !finished,packet.count==613,packet[0]==5 else { return false }
                let sequence=UInt32(packet[5])|UInt32(packet[6])<<8|UInt32(packet[7])<<16|UInt32(packet[8])<<24
                guard sequence==expected else { finished=true;done.signal();return false }
                expected+=1
                queue.async {
                    if expected>=40 { passed=true;finished=true;relay.stop();done.signal() }
                    else { relay.update(["stream":UInt32(777),"next":expected,"limit":expected+26]) }
                }
                return true
            }
            relay.open(["stream":UInt32(777),"url":address,"rate":24000,"samples":1200,"next":UInt32(0),"limit":UInt32(26)])
        }
        _ = done.wait(timeout:.now()+25)
        queue.sync { relay.stop() }
        guard passed else { fputs("Radio relay live probe: FAIL\n",stderr);exit(1) }
        print("Radio relay live probe: bundled decoder -> PCM -> ADPCM -> credit-paced packets PASS")
    }
    static func run(_ check:(Bool,String)->Void) {
        var index=0
        let silence=RadioWire.encode(Data(repeating:0,count:2400),index:&index)!
        check(silence.count==604 && silence.allSatisfy{$0==0},"24 kHz radio silence codec")
        check(RadioWire.encode(Data(repeating:0,count:2399),index:&index)==nil,"partial PCM is never encoded")
        let packet=RadioWire.packet(stream:0x12345678,sequence:0x04030201,block:silence)
        check(packet.count==613 && Array(packet.prefix(9))==[5,0x78,0x56,0x34,0x12,1,2,3,4],"radio binary envelope")
        var credit=RadioCredit();let at=Date()
        check(credit.accept(next:0,limit:39,now:at),"initial device window")
        for _ in 0..<39 { credit.sent() }
        check(!credit.accept(next:40,limit:70,now:at),"reject acknowledgment for unsent data")
        check(credit.accept(next:12,limit:43,now:at),"cumulative credit advances")
        check(!credit.accept(next:11,limit:43,now:at),"ignore reordered acknowledgment")
        check(!credit.accept(next:12,limit:42,now:at),"ignore reordered window")
        check(!credit.accept(next:12,limit:1000,now:at),"bounded device window")
        check(credit.accept(next:12,limit:43,now:at.addingTimeInterval(1)) && credit.next==12,"bounded resend from first missing block")
        check(credit.accept(next:39,limit:65,now:at.addingTimeInterval(1.1)),"late cumulative acknowledgment remains valid during retry")
        let args=RadioRelay.arguments(url:"https://example.org/live?x=1&y=2")
        check(args[args.firstIndex(of:"-i")!+1]=="https://example.org/live?x=1&y=2","URL is an argument, no shell interpolation")
        check(args.contains("http,https,tcp,tls,crypto") && !args.contains("file"),"network protocol whitelist")
        check(args[args.firstIndex(of:"-tls_verify")!+1]=="1","relay verifies TLS certificates")
        check(RadioRelay.sourceFailure(Data("https://user:secret@host/private?token=secret: Server returned 403 Forbidden".utf8))?.1==403,"HTTP classification extracts only status")
        check(RadioRelay.sourceFailure(Data("SSL certificate problem for private-url".utf8))?.0 == .sourceTLS,"TLS classification without raw stderr")
        if let i=CommandLine.arguments.firstIndex(of:"--radio-vectors"),CommandLine.arguments.count>i+1,
           let vector=try? Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[i+1])) {
            index=0
            check(vector.count==3004 && RadioWire.encode(vector.suffix(2400),index:&index)==vector.prefix(604),"C / Swift radio encoder shared vector")
        }
    }
}
