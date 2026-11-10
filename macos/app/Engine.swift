import Foundation
import Darwin
struct ConnectionState {
    var message="正在查找设备…",device="",address="",paired=false,connected=false,recording=false
    var permission=false,audio=false,packets=0,dropped=0,error=""
    var powerAvailable=false,powerLoading=false,powerError=""
    var powerSnapshot:PowerSnapshot?
}

final class Engine {
    private let work=DispatchQueue(label:"vibe.network")
    private let keyboard=Shortcut(),sink=AudioSink()
    private var socketFD:Int32 = -1
    private var localPort:UInt16=0,socketGeneration=0
    private var reader:DispatchSourceRead?,timer:DispatchSourceTimer?
    private var peer:sockaddr_in?,device="",token="",candidateToken=""
    private var lastSeen=Date.distantPast,lastAudio=Date.distantPast,lastBeacon=Date.distantPast,lastKey=Date.distantPast
    private var generation=0,sequence:UInt8?,active=false,ending=false
    private var pending:[String:(body:[String:Any],tries:Int,at:Date,kind:String)]=[:]
    private var state=ConnectionState()
    private var shortcut="ctrl+option+v"
    private var recentEvents:[Int]=[]
    private var bootID:Int?
    private var settingsRevision:UInt32?
    private var agentCapable=false
    private var slowHeartbeat=false,lastReadiness=""
    private var peerTimeout:TimeInterval { LANSocket.peerTimeout(capable:slowHeartbeat) }
    private var agentPending:[String:(body:[String:Any],expires:Date,at:Date,tries:Int)]=[:]
    var onAgentResult:((String)->Void)?
    private var radioTransfer:RadioListTransfer?
    var onRadio:(([RadioStation],Int,Int)->Void)?
    var onRadioLoading:((Bool)->Void)?
    var onRadioVerified:((String)->Void)?
    var onState:((ConnectionState)->Void)?
    var onSettings:((DeviceSettings)->Void)?
    var onResult:((Bool,String)->Void)?
    init(preview:Bool=false) {
        if preview { return }
        device=UserDefaults.standard.string(forKey:"device") ?? ""
        token=PairingStore.load(device) ?? ""
        shortcut=UserDefaults.standard.string(forKey:"shortcut") ?? "ctrl+option+v"
    }
    func start() { work.async { self.open() } }
    private func publish() {
        state.device=device;state.connected=peer != nil && Date().timeIntervalSince(lastSeen)<peerTimeout
        state.paired = !token.isEmpty;state.recording=active;state.audio=AudioSink.deviceUID() != nil
        var copy=state
        DispatchQueue.main.async {
            copy.permission=Shortcut.permitted
            copy.message=copy.recording ? "正在语音输入" : !copy.connected ? "正在查找设备…" : !copy.paired ? "输入设备屏幕上的配对码" : !copy.permission || !copy.audio ? "请完成首次使用设置" : "已连接"
            self.onState?(copy)
        }
    }
    private func open() {
        guard socketFD<0 else { return }
        let connection:LANSocket
        do { connection=try LANSocket.open() } catch { state.error=error.localizedDescription;publish();return }
        let fd=connection.descriptor;socketFD=fd;localPort=connection.port;state.error=""
        lastBeacon = .distantPast;socketGeneration+=1;let opened=socketGeneration
        let source=DispatchSource.makeReadSource(fileDescriptor:fd,queue:work)
        source.setEventHandler { [weak self] in guard let self=self,self.socketGeneration==opened else { return };self.receive() };source.setCancelHandler { Darwin.close(fd) };reader=source;source.resume()
        let clock=DispatchSource.makeTimerSource(queue:work);clock.schedule(deadline:.now(),repeating:.milliseconds(500),leeway:.milliseconds(100))
        clock.setEventHandler { [weak self] in self?.tick() };timer=clock;clock.resume();publish()
    }
    func retry() { work.async { if self.socketFD<0 { self.open() } else { self.beacon(broadcast:true);self.lastBeacon=Date();self.publish() } } }
    private func send(_ body:[String:Any],type:UInt8=2,to target:sockaddr_in?=nil) {
        guard socketFD>=0,let data=try? JSONSerialization.data(withJSONObject:body), data.count<=1536 else { return }
        var packet=Data([type]);packet.append(data)
        LANSocket.send(packet,from:socketFD,to:target ?? peer ?? sockaddr_in())
    }
    private func destinations() -> [sockaddr_in] {
        var result:[sockaddr_in]=[];var head:UnsafeMutablePointer<ifaddrs>?
        if getifaddrs(&head)==0 {
            var item=head
            while let node=item {
                let iface=node.pointee
                if let addr=iface.ifa_addr,let mask=iface.ifa_netmask,addr.pointee.sa_family==UInt8(AF_INET), iface.ifa_flags & UInt32(IFF_UP|IFF_BROADCAST)==UInt32(IFF_UP|IFF_BROADCAST) {
                    let ip=UnsafeRawPointer(addr).assumingMemoryBound(to:sockaddr_in.self).pointee
                    let net=UnsafeRawPointer(mask).assumingMemoryBound(to:sockaddr_in.self).pointee
                    var b=ip;b.sin_addr.s_addr=ip.sin_addr.s_addr | ~net.sin_addr.s_addr;b.sin_port=LANSocket.discoveryPort.bigEndian
                    if !result.contains(where:{$0.sin_addr.s_addr==b.sin_addr.s_addr}) { result.append(b) }
                }
                item=iface.ifa_next
            }
            freeifaddrs(head)
        }
        if result.isEmpty {
            var all=sockaddr_in();all.sin_len=UInt8(MemoryLayout<sockaddr_in>.size);all.sin_family=sa_family_t(AF_INET);all.sin_addr.s_addr=INADDR_BROADCAST;all.sin_port=LANSocket.discoveryPort.bigEndian;result.append(all)
        }
        return result
    }
    private func beacon(broadcast:Bool) {
        let body=LANSocket.beacon(port:localPort,token:token,audioReady:AudioSink.deviceUID() != nil,
            hotkeyReady:Shortcut.permitted,epoch:Int(Date().timeIntervalSince1970),tz:TimeZone.current.secondsFromGMT()/60)
        if broadcast { for dest in destinations() { send(body,type:3,to:dest) } }
        else { send(body,type:3) }
    }
    private func sendStatus() {
        if !token.isEmpty { send(["type":"pc.status","token":token,"audioReady":AudioSink.deviceUID() != nil,"hotkeyReady":Shortcut.permitted,"epoch":Int(Date().timeIntervalSince1970),"tz":TimeZone.current.secondsFromGMT()/60,"idleHeartbeat":LANSocket.negotiatedHeartbeatSeconds]) }
    }
    func notifyAgent(_ event:AgentEvent) {
        work.async {
            guard self.peer != nil,!self.token.isEmpty,Date().timeIntervalSince(self.lastSeen)<self.peerTimeout else { DispatchQueue.main.async{self.onAgentResult?("设备离线，本次提醒未发送")};return }
            guard self.agentCapable else { DispatchQueue.main.async{self.onAgentResult?("请先更新支持 Agent 提醒的设备固件")};return }
            guard self.agentPending.count<8 else { return }
            var body=event.deviceBody;body["type"]="agent.notify";body["id"]=event.id;body["token"]=self.token
            body["ttl_ms"]=10000;body["expires"]=Date().timeIntervalSince1970+10
            self.agentPending[event.id]=(body,Date().addingTimeInterval(5),Date(),1);self.send(body)
        }
    }
    private func tick() {
        let now=Date()
        if now.timeIntervalSince(lastSeen)>peerTimeout,peer != nil { disconnect("设备已断开，未保存的修改已保留") }
        if active && (now.timeIntervalSince(lastAudio)>3 || now.timeIntervalSince(lastKey)>180 || !Shortcut.permitted) { endVoice(immediate:true) }
        let readiness="\(AudioSink.deviceUID() != nil)/\(Shortcut.permitted)/\(TimeZone.current.secondsFromGMT())"
        let changed=readiness != lastReadiness;lastReadiness=readiness
        if now.timeIntervalSince(lastBeacon)>((peer == nil || token.isEmpty) ? 2 : LANSocket.heartbeat(capable:slowHeartbeat)) || (changed && peer != nil) {
            beacon(broadcast:peer == nil);lastBeacon=now
        }
        for (id,entry) in pending where now.timeIntervalSince(entry.at)>1.2 {
            if entry.tries>=4 { pending.removeValue(forKey:id);if entry.kind.hasPrefix("power.") { state.powerLoading=false;state.powerError="读取超时，请重试；下方保留上次读取结果。" } else if entry.kind.hasPrefix("radio.") { cancelRadio("电台同步超时，原列表已保留，请重新读取") } else { DispatchQueue.main.async { self.onResult?(false,"设备没有响应，请检查连接后重试") } } }
            else { send(entry.body);pending[id]=(entry.body,entry.tries+1,now,entry.kind) }
        }
        for (id,entry) in agentPending where now.timeIntervalSince(entry.at)>0.8 {
            if now>entry.expires || entry.tries>=4 { agentPending.removeValue(forKey:id);DispatchQueue.main.async{self.onAgentResult?("设备未确认提醒，请检查连接")} }
            else { send(entry.body);agentPending[id]=(entry.body,entry.expires,now,entry.tries+1) }
        }
        publish()
    }
    private func receive() {
        var bytes=[UInt8](repeating:0,count:2048)
        for _ in 0..<32 {
            var from=sockaddr_in();var size=socklen_t(MemoryLayout<sockaddr_in>.size)
            let n=withUnsafeMutablePointer(to:&from) { p in p.withMemoryRebound(to:sockaddr.self,capacity:1) { Darwin.recvfrom(socketFD,&bytes,bytes.count,0,$0,&size) } }
            if n<=0 { break }
            let data=Data(bytes.prefix(n))
            guard from.sin_family==sa_family_t(AF_INET),from.sin_port==LANSocket.discoveryPort.bigEndian else { continue }
            guard let packet=Packet.parse(data) else { continue }
            if case .event(let body)=packet,body["event"] as? String=="device.hello",let found=body["device"] as? String {
                guard found.count==12,found.allSatisfy({ $0.isHexDigit }),device.isEmpty || found==device else { continue }
                let changed=peer.map { !LANSocket.sameEndpoint($0,from) } ?? false
                let rebooted=bootID != nil && (body["boot"] as? Int) != bootID
                if changed || rebooted { disconnect("设备重新连接，请确认未完成的保存") }
                agentCapable=body["agentNotify"] as? Int==1
                // The firmware advertises the upper bound it accepts (30),
                // not the interval chosen in this client's status message.
                slowHeartbeat=body["idleHeartbeat"] as? Int==30
                state.powerAvailable=body["powerDiagnostics"] as? Int==1
                let reconnect=peer==nil
                if reconnect || (body["boot"] as? Int) != bootID { recentEvents=[];bootID=body["boot"] as? Int;settingsRevision=nil }
                peer=from;device=found
                if body["paired"] as? Bool==false && !token.isEmpty {
                    endVoice(immediate:true);token="";UserDefaults.standard.removeObject(forKey:"device")
                    state.error="设备已解除配对，请输入新的配对码"
                } else if body["paired"] as? Bool==true && token.isEmpty {
                    state.error="设备已与其他电脑配对。请先在设备设置中解除电脑配对。"
                }
                var ip=from.sin_addr;var text=[CChar](repeating:0,count:Int(INET_ADDRSTRLEN));inet_ntop(AF_INET,&ip,&text,socklen_t(text.count));state.address=String(cString:text)
                lastSeen=Date();if reconnect && body["statusBeacon"] as? Bool != true { sendStatus() }
                let revision=(body["settingsRevision"] as? NSNumber)?.uint32Value
                if (reconnect || (revision != nil && revision != settingsRevision)) && !token.isEmpty && pending.isEmpty { request("config.get",extra:[:]) }
            }
            guard let selected=peer,LANSocket.sameEndpoint(selected,from) else { continue }
            lastSeen=Date()
            switch packet {
            case .ping: break
            case .audio(let seq,let pcm):
                guard active else { continue }
                if let previous=sequence {
                    let delta=seq &- previous
                    if delta==0 || delta>128 { continue }
                    if delta>1 { state.dropped+=Int(delta)-1;for _ in 0..<min(3,Int(delta)-1) { sink.append([Int16](repeating:0,count:1600)) } }
                }
                sequence=seq;state.packets+=1;lastAudio=Date();sink.append(pcm)
            case .event(let body): handle(body)
            }
        }
        publish()
    }
    private func handle(_ body:[String:Any]) {
        if let id=body["eid"] as? Int {
            if recentEvents.contains(id) { return }
            recentEvents.append(id);if recentEvents.count>64 { recentEvents.removeFirst() }
        }
        switch body["event"] as? String ?? "" {
        case "agent.reply":
            guard let id=body["id"] as? String,agentPending.removeValue(forKey:id) != nil else { return }
            let ok=body["ok"] as? Bool==true
            DispatchQueue.main.async{self.onAgentResult?(ok ? "设备已接收提醒" : "设备未接受提醒")}
        case "voice.start":
            if active && ending { endVoice(immediate:true) }
            guard !token.isEmpty,!active else { return }
            do { try sink.start();try keyboard.start(shortcut);active=true;ending=false;generation+=1;sequence=nil;lastAudio=Date();lastKey=Date();state.error="" }
            catch { sink.stop();keyboard.stop();state.error=error.localizedDescription }
        case "voice.end": endVoice(immediate:false)
        case "key.action":
            if !token.isEmpty,!active,body["action"] as? String=="enter" { keyboard.enter() }
        case "radio.verified":
            if let url=body["url"] as? String { DispatchQueue.main.async { self.onRadioVerified?(url) } }
        case "radio.reply":
            guard let id=body["id"] as? String,let entry=pending[id],entry.kind.hasPrefix("radio."),var transfer=radioTransfer else { return }
            pending.removeValue(forKey:id)
            if body["verified"] as? Bool==true,let station=body["station"] as? [String:Any],let url=station["url"] as? String { DispatchQueue.main.async { self.onRadioVerified?(url) } }
            do {
                if try transfer.accept(body) {
                    radioTransfer=nil
                    DispatchQueue.main.async { self.onRadio?(transfer.stations,transfer.revision,transfer.capacity);self.onRadioLoading?(false);if transfer.writing { self.onResult?(true,"电台及顺序已保存到设备") } }
                } else { radioTransfer=transfer;sendRadioStep() }
            } catch { cancelRadio(error.localizedDescription) }
        case "power.reply":
            guard let id=body["id"] as? String,let entry=pending[id],entry.kind.hasPrefix("power.") else { return }
            pending.removeValue(forKey:id);state.powerLoading=false
            state.powerSnapshot=PowerSnapshot(body);state.powerError=""
        case "config.reply":
            guard let id=body["id"] as? String,let entry=pending.removeValue(forKey:id) else { return }
            let ok=body["ok"] as? Bool ?? false
            if ok,entry.kind=="pair" {
                guard PairingStore.save(candidateToken,device:device) else { DispatchQueue.main.async { self.onResult?(false,"无法保存本地配对信息，请检查当前账户的文件权限后重试") };return }
                token=candidateToken;candidateToken="";UserDefaults.standard.set(device,forKey:"device");beacon(broadcast:false)
            }
            if ok { receiveSettings(body) }
            if ok,entry.kind=="config.get",radioTransfer==nil,(body["settings"] as? [String:Any])?["radio_list_version"] as? Int==1 { beginRadioRead() }
            if entry.kind != "config.get" { DispatchQueue.main.async { self.onResult?(ok,ok ? "已保存到设备" : (body["error"] as? String ?? "设备拒绝了这次操作")) } }
        case "config.changed":
            if !token.isEmpty { receiveSettings(body) }
        default: break
        }
    }
    private func receiveSettings(_ body:[String:Any]) {
        guard let settings=body["settings"],let data=try? JSONSerialization.data(withJSONObject:settings),var prefs=try? JSONDecoder().decode(DeviceSettings.self,from:data),prefs.valid else { return }
        prefs.requireWeather();settingsRevision=(body["settingsRevision"] as? NSNumber)?.uint32Value
        let value=prefs;DispatchQueue.main.async { self.onSettings?(value) }
    }
    private func endVoice(immediate:Bool) {
        guard active else { return }
        ending=true
        generation+=1;let expected=generation
        let stop={ [self] in guard generation==expected else { return };keyboard.stop();sink.stop();active=false;ending=false;sequence=nil;publish() }
        if immediate { stop() } else { work.asyncAfter(deadline:.now()+0.18,execute:stop) }
    }
    private func request(_ type:String,extra:[String:Any]) {
        let id=UUID().uuidString
        var body=extra;body["type"]=type;body["id"]=id;body["token"]=type=="pair" ? candidateToken : token
        pending[id]=(body,1,Date(),type);send(body)
    }
    func pair(_ code:String) {
        work.async {
            guard self.peer != nil,code.count==6,code.allSatisfy({ $0.isNumber }) else { DispatchQueue.main.async { self.onResult?(false,"请先找到设备，并输入屏幕上的六位配对码") };return }
            if self.candidateToken.isEmpty { self.candidateToken=PairingStore.random() }
            self.request("pair",extra:["code":code])
        }
    }
    func save(_ prefs:DeviceSettings,changes:[String:Any]) { work.async { guard prefs.validForSave,!self.token.isEmpty else { return };self.request("config.set",extra:["settings":changes]) } }
    func reload() { work.async { if !self.token.isEmpty { self.request("config.get",extra:[:]) } } }
    func readPower(reset:Bool=false) {
        work.async {
            guard self.peer != nil,!self.token.isEmpty,self.state.powerAvailable,!self.state.powerLoading else { return }
            self.state.powerLoading=true;self.state.powerError="";self.request(reset ? "power.reset" : "power.get",extra:[:]);self.publish()
        }
    }
    private func sendRadioStep() {
        guard let transfer=radioTransfer else { return }
        let (type,extra)=transfer.command;request(type,extra:extra)
    }
    private func beginRadioRead() {
        guard !token.isEmpty,peer != nil,radioTransfer==nil else { return }
        radioTransfer=RadioListTransfer();DispatchQueue.main.async { self.onRadioLoading?(true) };sendRadioStep()
    }
    private func cancelRadio(_ message:String) {
        guard let transfer=radioTransfer else { return }
        if transfer.writing { send(["type":"radio.abort","transaction":transfer.transaction,"token":token]) }
        radioTransfer=nil;pending=pending.filter { !$0.value.kind.hasPrefix("radio.") }
        DispatchQueue.main.async { self.onRadioLoading?(false);self.onResult?(false,message) }
    }
    func reloadRadio() { work.async { self.beginRadioRead() } }
    func saveRadio(_ stations:[RadioStation],revision:Int,capacity:Int) {
        work.async {
            guard self.peer != nil,!self.token.isEmpty,!self.active,self.radioTransfer==nil else { DispatchQueue.main.async { self.onResult?(false,"请等待设备连接及电台读取完成，并结束语音输入") };return }
            do { self.radioTransfer=try RadioListTransfer(stations:stations,revision:revision,capacity:capacity);DispatchQueue.main.async { self.onRadioLoading?(true) };self.sendRadioStep() }
            catch { DispatchQueue.main.async { self.onResult?(false,error.localizedDescription) } }
        }
    }
    func setShortcut(_ spec:String) throws {
        _=try Shortcut.resolve(spec);UserDefaults.standard.set(spec,forKey:"shortcut")
        work.async { self.endVoice(immediate:true);self.shortcut=spec }
    }
    private func disconnect(_ message:String) {
        slowHeartbeat=false;state.powerAvailable=false;state.powerLoading=false
        agentPending.removeAll();agentCapable=false;
        cancelRadio(message)
        let interrupted = !pending.isEmpty
        pending.removeAll();peer=nil;state.address="";lastSeen = .distantPast;lastBeacon = .distantPast
        settingsRevision=nil;bootID=nil;recentEvents.removeAll();endVoice(immediate:true)
        if interrupted { DispatchQueue.main.async { self.onResult?(false,message) } }
    }
    func stop(completion:(()->Void)?=nil) {
        work.sync {
            if !token.isEmpty { send(["type":"pc.status","token":token,"audioReady":false,"hotkeyReady":false]) }
            disconnect("连接已结束，未确认的修改请重新读取");
            keyboard.stop();sink.stop();timer?.cancel();timer=nil;socketGeneration+=1
            if let reader=reader {
                let fd=socketFD
                reader.setCancelHandler { Darwin.close(fd);if let completion=completion { DispatchQueue.main.async(execute:completion) } }
                reader.cancel()
            } else if let completion=completion { DispatchQueue.main.async(execute:completion) }
            reader=nil;socketFD = -1;localPort=0
        }
    }
}
