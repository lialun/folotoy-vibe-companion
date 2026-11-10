import Foundation
import AudioToolbox

// A bounded, silent local probe. It never sends playback commands to the toy.
enum RadioMedia {
    struct Format { let codec:String,sampleRate:Int,channels:Int }
    static func audio(_ bytes:Data)->Format? {
        let b=Array(bytes);guard b.count>=8 else { return nil }
        for i in 0..<min(b.count-7,8192) where b[i]==255 && b[i+1]&0xe0==0xe0 {
            if b[i+1]&0xf6==0xf0 { // ADTS; device's conservative AAC-LC subset.
                let rates=[96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350]
                let index=Int((b[i+2]>>2)&15),profile=Int(b[i+2]>>6)
                let channels=Int((b[i+2]&1)<<2 | b[i+3]>>6)
                let size=Int(b[i+3]&3)<<11 | Int(b[i+4])<<3 | Int(b[i+5]>>5)
                if index<rates.count,profile==1,(8000...48000).contains(rates[index]),(1...2).contains(channels),size>=7,i+size+2<b.count,b[i+size]==255,b[i+size+1]&0xf6==0xf0 {
                    return Format(codec:"AAC-LC",sampleRate:rates[index],channels:channels)
                }
            } else {
                let version=Int((b[i+1]>>3)&3),layer=Int((b[i+1]>>1)&3),rateIndex=Int((b[i+2]>>2)&3),bitrateIndex=Int(b[i+2]>>4)
                guard version != 1,layer==1,rateIndex<3,(1...14).contains(bitrateIndex) else { continue }
                let bitrates=version==3 ? [0,32,40,48,56,64,80,96,112,128,160,192,224,256,320] : [0,8,16,24,32,40,48,56,64,80,96,112,128,144,160]
                let rate=[44100,48000,32000][rateIndex]/(version==3 ? 1 : version==2 ? 2 : 4)
                let size=(version==3 ? 144000 : 72000)*bitrates[bitrateIndex]/rate+Int((b[i+2]>>1)&1)
                if (8000...48000).contains(rate),i+size+3<b.count,b[i+size]==255,b[i+size+1]&0xfe==b[i+1]&0xfe {
                    return Format(codec:"MP3",sampleRate:rate,channels:b[i+3]>>6==3 ? 1 : 2)
                }
            }
        }
        return nil
    }
    static func transportAudio(_ data:Data)->Data? {
        let b=Array(data);guard b.count>=564,b[0]==0x47,b[188]==0x47,b[376]==0x47 else { return nil }
        var pid:Int?,result=Data()
        for base in stride(from:0,through:b.count-188,by:188) {
            guard b[base]==0x47,b[base+1]&0x80==0,b[base+3]&0xc0==0 else { return nil }
            let packetPID=Int(b[base+1]&31)<<8 | Int(b[base+2]),mode=(b[base+3]>>4)&3
            if mode==0 || mode==2 { continue }
            var start=base+4
            if mode==3 { start+=1+Int(b[start]) }
            guard start<base+188 else { continue }
            if b[base+1]&0x40 != 0 {
                guard start+9<base+188,b[start]==0,b[start+1]==0,b[start+2]==1,(0xc0...0xdf).contains(b[start+3]) else { continue }
                if pid==nil { pid=packetPID }
                start+=9+Int(b[start+8])
            }
            if packetPID==pid,start<base+188 { result.append(contentsOf:b[start..<base+188]) }
        }
        return result.isEmpty ? nil : result
    }
    static func nativePackets(_ data:Data,format:Format)->Bool {
        var packets=0
        return withUnsafeMutablePointer(to:&packets) { count in
            var parser:AudioFileStreamID?
            let hint = format.codec=="MP3" ? kAudioFileMP3Type : kAudioFileAAC_ADTSType
            let status=AudioFileStreamOpen(count,{ _,_,_,_ in },{ context,_,n,_,_ in
                context.assumingMemoryBound(to:Int.self).pointee+=Int(n)
            },hint,&parser)
            guard status==noErr,let stream=parser else { return false }
            defer { AudioFileStreamClose(stream) }
            let parsed=data.withUnsafeBytes { bytes in AudioFileStreamParseBytes(stream,UInt32(bytes.count),bytes.baseAddress!,[]) }
            return parsed==noErr && count.pointee>0
        }
    }
}

private final class RadioProbeFetch:NSObject,URLSessionDataDelegate {
    var data=Data(),response:HTTPURLResponse?,error:Error?
    private let signal=DispatchSemaphore(value:0)
    private var limit=131072,redirects=0,finished=false
    func fetch(_ url:URL,limit:Int,timeout:TimeInterval)throws->(Data,HTTPURLResponse) {
        self.limit=limit
        let configuration=URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest=timeout;configuration.timeoutIntervalForResource=timeout
        configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        let queue=OperationQueue();queue.maxConcurrentOperationCount=1
        let session=URLSession(configuration:configuration,delegate:self,delegateQueue:queue)
        var request=URLRequest(url:url);request.setValue(AppMetadata.userAgent,forHTTPHeaderField:"User-Agent")
        request.setValue("0",forHTTPHeaderField:"Icy-MetaData");request.setValue("identity",forHTTPHeaderField:"Accept-Encoding")
        let task=session.dataTask(with:request);task.resume()
        let timedOut=signal.wait(timeout:.now()+timeout) == .timedOut
        task.cancel();session.invalidateAndCancel();queue.waitUntilAllOperationsAreFinished()
        if timedOut { throw RadioFailure.message("检测超时，尚未确认音频格式，请重试") }
        if let error=error { throw error }
        guard let response=response,!data.isEmpty else { throw RadioFailure.message("没有收到有效音频数据") }
        return (data,response)
    }
    func urlSession(_ session:URLSession,dataTask:URLSessionDataTask,didReceive response:URLResponse,completionHandler:@escaping(URLSession.ResponseDisposition)->Void) {
        guard let http=response as? HTTPURLResponse,(200...299).contains(http.statusCode) else {
            let code=(response as? HTTPURLResponse)?.statusCode ?? 0
            error=RadioFailure.message("电台服务器返回 \(code)");completionHandler(.cancel);return
        }
        self.response=http;completionHandler(.allow)
    }
    func urlSession(_ session:URLSession,dataTask:URLSessionDataTask,didReceive chunk:Data) {
        guard !finished else { return };data.append(chunk.prefix(limit-data.count))
        // Stop early once complete frames are available; low-bitrate live streams
        // must not wait for the entire byte budget to arrive.
        let audio=RadioMedia.transportAudio(data) ?? data
        let complete=data.count>=4096 && RadioMedia.audio(audio) != nil
        if complete || data.count>=limit { finished=true;dataTask.cancel();signal.signal() }
    }
    func urlSession(_ session:URLSession,task:URLSessionTask,didCompleteWithError error:Error?) {
        if !finished { self.error=self.error ?? error;finished=true;signal.signal() }
    }
    func urlSession(_ session:URLSession,task:URLSessionTask,willPerformHTTPRedirection response:HTTPURLResponse,newRequest request:URLRequest,completionHandler:@escaping(URLRequest?)->Void) {
        redirects+=1
        if redirects>4 || !["http","https"].contains(request.url?.scheme?.lowercased() ?? "") { error=RadioFailure.message("电台跳转次数过多或地址不受支持");completionHandler(nil) }
        else { completionHandler(request) }
    }
}

enum RadioProbe {
    static func check(_ station:RadioStation,completion:@escaping(Result<RadioStation,Error>)->Void) {
        DispatchQueue.global(qos:.utility).async {
            let result=Result { try inspect(station,deadline:Date().addingTimeInterval(20),depth:0,hls:false) }
            DispatchQueue.main.async { completion(result) }
        }
    }
    static func inspect(_ station:RadioStation,deadline:Date,depth:Int,hls:Bool)throws->RadioStation {
        guard station.valid,depth<4,let url=URL(string:station.url) else { throw RadioFailure.message("直播地址或播放列表层级不受支持") }
        let timeout=deadline.timeIntervalSinceNow;guard timeout>0 else { throw RadioFailure.message("检测超时，请重试") }
        let (data,response)=try RadioProbeFetch().fetch(url,limit:hls ? 131072:32768,timeout:min(10,timeout))
        let base=response.url ?? url
        let prefix=String(decoding:data.prefix(256),as:UTF8.self).trimmingCharacters(in:.whitespacesAndNewlines).lowercased()
        if prefix.hasPrefix("<!doctype") || prefix.hasPrefix("<html") { throw RadioFailure.message("这是网页地址，请填写直播音频地址") }
        if prefix.hasPrefix("#extm3u") || prefix.hasPrefix("[playlist]") {
            guard let list=String(data:data,encoding:.utf8),data.count<32768 else { throw RadioFailure.message("播放列表过大或格式无效") }
            let lines=list.components(separatedBy:.newlines).map { $0.trimmingCharacters(in:.whitespacesAndNewlines) }
            let isHLS=lines.contains { $0.hasPrefix("#EXT-X-") }
            if lines.contains(where:{ ($0.hasPrefix("#EXT-X-KEY:") && $0 != "#EXT-X-KEY:METHOD=NONE") || $0.hasPrefix("#EXT-X-MAP:") || $0.hasPrefix("#EXT-X-BYTERANGE:") }) { throw RadioFailure.message("设备暂不支持此 HLS 加密或分片格式") }
            var next:String?
            if lines.contains(where:{$0.hasPrefix("#EXT-X-STREAM-INF:")}) {
                var best=Int.max
                for (i,line) in lines.enumerated() where line.hasPrefix("#EXT-X-STREAM-INF:") && i+1<lines.count {
                    let lower=line.lowercased()
                    guard !lower.contains("resolution="),!lower.contains("avc"),!lower.contains("hvc"),!lower.contains("hev"),!lower.contains("vp09"),!lower.contains("av01"),lower.contains("mp4a") || lower.contains("mp3") else { continue }
                    let value=line.components(separatedBy:"BANDWIDTH=").dropFirst().first.map { String($0.prefix(while:{$0.isNumber})) } ?? ""
                    let bandwidth=Int(value) ?? 0
                    if bandwidth>0,bandwidth<=192000,bandwidth<best { next=lines[i+1];best=bandwidth }
                }
            } else if isHLS { next=lines.last(where:{!$0.isEmpty && !$0.hasPrefix("#")}) }
            else if prefix.hasPrefix("[playlist]") { next=lines.first(where:{$0.lowercased().hasPrefix("file1=")}).map { String($0.dropFirst(6)) } }
            else { next=lines.first(where:{!$0.isEmpty && !$0.hasPrefix("#")}) }
            guard let path=next,let target=URL(string:path,relativeTo:base)?.absoluteURL else { throw RadioFailure.message("未找到设备支持的音频流") }
            let checked=try inspect(RadioStation(name:station.name,url:target.absoluteString),deadline:deadline,depth:depth+1,hls:isHLS)
            return isHLS ? station : checked // Retain stable playlist URLs, never an expiring segment URL.
        }
        let audio=RadioMedia.transportAudio(data) ?? data
        guard let format=RadioMedia.audio(audio),RadioMedia.nativePackets(audio,format:format) else { throw RadioFailure.message("尚未识别到设备支持的有效 MP3／AAC 音频，请检查地址或重试") }
        return station
    }
}
