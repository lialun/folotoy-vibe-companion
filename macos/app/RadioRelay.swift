import Foundation
import Darwin

enum RadioWire {
    static let rate=24000,samples=1200,bytes=604
    static func packet(stream:UInt32,sequence:UInt32,block:Data)->Data {
        guard block.count==bytes else { return Data() }
        var data=Data([5])
        for value in [stream,sequence] { for shift in stride(from:0,to:32,by:8) { data.append(UInt8(truncatingIfNeeded:value>>shift)) } }
        data.append(block);return data
    }
    // Same independent IMA blocks as the resident device codec, 50 ms at 24 kHz.
    static func encode(_ pcm:Data,index:inout Int)->Data? {
        guard pcm.count==samples*2 else { return nil }
        let input=Array(pcm)
        func sample(_ n:Int)->Int { Int(Int16(bitPattern:UInt16(input[n*2])|UInt16(input[n*2+1])<<8)) }
        var predictor=sample(0)
        var out=Data([input[0],input[1],UInt8(index),0])
        var low:UInt8=0
        for n in 1..<samples {
            let step=VoiceCodec.steps[index],difference=sample(n)-predictor
            var remaining=abs(difference),code=difference<0 ? 8:0,delta=step>>3
            if remaining>=step { code|=4;remaining-=step;delta+=step }
            if remaining>=step>>1 { code|=2;remaining-=step>>1;delta+=step>>1 }
            if remaining>=step>>2 { code|=1;delta+=step>>2 }
            predictor=max(-32768,min(32767,predictor+(code&8 != 0 ? -delta:delta)))
            index=max(0,min(88,index+VoiceCodec.changes[code]))
            if n%2==1 { low=UInt8(code) } else { out.append(low|UInt8(code<<4)) }
        }
        out.append(low);return out
    }
}

/* Bounded cumulative credit window. Shared by Wi-Fi and BLE; a stale credit
 * cannot rewind a stream or grow its receiver budget. */
struct RadioCredit {
    private(set) var acknowledged:UInt32=0,limit:UInt32=0,next:UInt32=0
    private var progress=Date.distantPast,highest:UInt32=0
    mutating func accept(next received:UInt32,limit end:UInt32,now:Date=Date())->Bool {
        guard received>=acknowledged,received<=highest,end>=received,end-received<=39,end>=limit else { return false }
        if received>acknowledged { progress=now }
        acknowledged=received;limit=end
        if now.timeIntervalSince(progress)>0.5 { next=received;progress=now }
        return true
    }
    mutating func sent() { next+=1;highest=max(highest,next) }
}

/* One producer subprocess and one bounded encoded queue, only while listening.
 * Engine queue owns process/session/credit; NSCondition protects producer data.
 * No stdout/stderr accumulation and no source URL or credentials in logs. */
final class RadioRelay {
    private final class Feed {
        let condition=NSCondition()
        var blocks=[Data](),base:UInt32=0,stopped=false,ended=false
        var errorBytes=Data(),exitStatus:Int32?,errorEnded=false
        func stop() { condition.lock();stopped=true;blocks.removeAll();condition.broadcast();condition.unlock() }
        func append(_ block:Data)->Bool {
            condition.lock();defer { condition.unlock() }
            while blocks.count>=200 && !stopped { _ = condition.wait(until:Date().addingTimeInterval(0.2)) }
            guard !stopped else { return false };blocks.append(block);return true
        }
        func block(_ sequence:UInt32,acknowledged:UInt32)->Data? {
            condition.lock();defer { condition.unlock() }
            if acknowledged>base,Int(acknowledged-base)<=blocks.count {
                blocks.removeFirst(Int(acknowledged-base));base=acknowledged;condition.signal()
            }
            guard sequence>=base,Int(sequence-base)<blocks.count else { return nil }
            return blocks[Int(sequence-base)]
        }
    }
    private let queue:DispatchQueue
    private var process:Process?,feed:Feed?,timer:DispatchSourceTimer?
    private(set) var stream:UInt32=0
    private var credit=RadioCredit(),lastCredit=Date.distantPast,opened=Date.distantPast
    private var station=""
    var send:((Data)->Bool)?,onFailure:((UInt32,DiagnosticRecord)->Void)?
    init(queue:DispatchQueue) { self.queue=queue }
    static var decoder:URL? { Bundle.main.url(forResource:"RadioDecoder",withExtension:nil) }
    static func arguments(url:String)->[String] {
        ["-nostdin","-hide_banner","-loglevel","error","-rw_timeout","10000000",
         "-protocol_whitelist","http,https,tcp,tls,crypto","-tls_verify","1","-i",url,
         "-map","0:a:0","-vn","-sn","-dn","-threads","1","-ac","1","-ar","24000","-f","s16le","pipe:1"]
    }
    func open(_ body:[String:Any]) {
        guard let value=body["stream"] as? UInt32,value>0,
              let address=body["url"] as? String,RadioStation(name:"Relay",url:address).valid,
              body["rate"] as? Int==RadioWire.rate,body["samples"] as? Int==RadioWire.samples else { return }
        if value==stream { update(body);return }
        stop();stream=value;credit=RadioCredit();opened=Date();lastCredit=Date();update(body)
        station=RadioStation.presets.first{$0.url==address}?.name ?? "自定义电台"
        guard let decoder=Self.decoder else { fail(.decoderMissing);return }
        let child=Process(),output=Pipe(),errors=Pipe(),input=Feed()
        child.executableURL=decoder;child.arguments=Self.arguments(url:address)
        child.standardOutput=output;child.standardError=errors;child.standardInput=FileHandle.nullDevice
        child.terminationHandler={ process in input.condition.lock();input.exitStatus=process.terminationStatus;input.condition.unlock() }
        do { try child.run() } catch { fail(.decoderStart);return }
        process=child;feed=input
        DispatchQueue.global(qos:.utility).async {
            defer { try? errors.fileHandleForReading.close();input.condition.lock();input.errorEnded=true;input.condition.unlock() }
            while let part=try? errors.fileHandleForReading.read(upToCount:2048),!part.isEmpty {
                input.condition.lock()
                input.errorBytes.append(part)
                if input.errorBytes.count>4096 { input.errorBytes.removeFirst(input.errorBytes.count-4096) }
                input.condition.unlock()
            }
        }
        DispatchQueue.global(qos:.utility).async {
            var pending=Data(),index=0
            defer {
                try? output.fileHandleForReading.close()
                input.condition.lock();input.ended=true;input.condition.broadcast();input.condition.unlock()
            }
            while let part=try? output.fileHandleForReading.read(upToCount:RadioWire.samples*2-pending.count),!part.isEmpty {
                pending.append(part)
                if pending.count==RadioWire.samples*2 {
                    guard let block=RadioWire.encode(pending,index:&index),input.append(block) else { break }
                    pending.removeAll(keepingCapacity:true)
                }
            }
        }
        let clock=DispatchSource.makeTimerSource(queue:queue)
        clock.schedule(deadline:.now(),repeating:.milliseconds(50),leeway:.milliseconds(10))
        clock.setEventHandler { [weak self] in self?.pump() };timer=clock;clock.resume()
    }
    func update(_ body:[String:Any]) {
        guard body["stream"] as? UInt32==stream,let next=body["next"] as? UInt32,let limit=body["limit"] as? UInt32,
              credit.accept(next:next,limit:limit) else { return }
        lastCredit=Date()
    }
    private func pump() {
        guard stream>0,let input=feed else { return }
        if Date().timeIntervalSince(lastCredit)>3 { fail(.creditTimeout);return }
        for _ in 0..<4 {
            guard credit.next<credit.limit,let block=input.block(credit.next,acknowledged:credit.acknowledged) else { break }
            guard send?(RadioWire.packet(stream:stream,sequence:credit.next,block:block))==true else { break }
            credit.sent()
        }
        input.condition.lock();let exhausted=input.ended && input.errorEnded && input.blocks.isEmpty;input.condition.unlock()
        if exhausted { fail(.sourceEnded) }
        else if Date().timeIntervalSince(opened)>20 && credit.next==0 { fail(.sourceTimeout) }
    }
    static func sourceFailure(_ data:Data)->(DiagnosticCode,Int?)? {
        let text=String(decoding:data,as:UTF8.self).lowercased()
        if text.contains("certificate") || text.contains("ssl") || text.contains("tls handshake") { return (.sourceTLS,nil) }
        if let range=text.range(of:"(?:http error |server returned |http/1\\.[01] )([45][0-9]{2})",options:.regularExpression) {
            let match=String(text[range]);let digits=match.suffix(3)
            return (.sourceHTTP,Int(digits))
        }
        if ["timed out","connection refused","network is unreachable","failed to resolve","nodename nor servname","connection reset","input/output error"].contains(where:text.contains) { return (.sourceNetwork,nil) }
        if ["invalid data","decoder not found","unknown decoder","error while decoding","protocol not on whitelist","unsupported codec","does not contain any stream"].contains(where:text.contains) { return (.sourceDecode,nil) }
        return nil
    }
    private func fail(_ fallback:DiagnosticCode) {
        let old=stream;guard old>0 else { return }
        var record=DiagnosticRecord(code:fallback);record.station=station
        record.numbers=["stream":Int64(old),"received":Int64(credit.acknowledged),"limit":Int64(credit.limit)]
        if let input=feed {
            input.condition.lock();let data=input.errorBytes,status=input.exitStatus;input.condition.unlock()
            if fallback != .creditTimeout,let (code,http)=Self.sourceFailure(data) { record.code=code;if let http=http { record.numbers["http"]=Int64(http) } }
            if let status=status { record.numbers["exit"]=Int64(status) }
        }
        stop();onFailure?(old,record)
    }
    func close(_ body:[String:Any]) { if body["stream"] as? UInt32==stream { stop() } }
    func stop() {
        timer?.cancel();timer=nil;stream=0;feed?.stop();feed=nil
        if let child=process,child.isRunning {
            child.terminate()
            // A blocked network read must never leave a decoder behind.
            queue.asyncAfter(deadline:.now()+2) { if child.isRunning { Darwin.kill(child.processIdentifier,SIGKILL) } }
        }
        process=nil
    }
}
