import Foundation
import Darwin
import os
enum LinkKind:String { case wifi,ble }
enum BluetoothPhase { case preparing,available,connecting,connected,parked,deviceDisabled,computerDisabled,unauthorized,unsupported,unavailable,failed }
struct ConnectionState {
    var message="正在查找设备…",device="",address="",paired=false,connected=false,recording=false
    var permission=false,audio=false,packets=0,dropped=0,error="",voiceError=""
    var owned=false,ownerName="",transport="",bluetooth="正在准备蓝牙…",wifiEnabled=true,bleEnabled=true
    var wifiConnected=false,bleConnected=false
    var bluetoothPhase=BluetoothPhase.preparing
    var powerAvailable=false,powerLoading=false,powerError=""
    var powerSnapshot:PowerSnapshot?
    var diagnosticsAvailable=false,diagnosticsLoading=false,diagnosticsError=""
    // A parked BLE client deliberately has no live route. It can still reconnect
    // and request takeover; reachability is not a prerequisite for this action.
    var canClaim:Bool { paired && !owned }
    var connectionTitle:String {
        if !connected { return paired ? "设备未连接" : "正在查找设备" }
        if !paired { return "找到设备，等待配对" }
        if !owned { return ownerName.isEmpty ? "等待连接到本机" : "正在由 \(ownerName) 使用" }
        return "已连接到本机"
    }
    func linkStatus(_ link:LinkKind)->String {
        let enabled=link == .wifi ? wifiEnabled : bleEnabled
        let reachable=link == .wifi ? wifiConnected : bleConnected
        guard enabled else { return "已关闭" }
        if reachable { return owned && connected && transport==(link == .wifi ? "Wi-Fi" : "蓝牙") ? "使用中" : "已连接" }
        if link == .ble {
            switch bluetoothPhase {
            case .parked:return "待机"
            case .unauthorized:return "未授权"
            case .computerDisabled:return "电脑已关闭"
            case .connecting:return "连接中"
            case .failed:return "异常"
            case .unsupported,.unavailable:return "不可用"
            default:break
            }
        }
        return "未连接"
    }
}

final class Engine {
    private let log=Logger(subsystem:"io.github.vibecompanion.mac",category:"Connection")
    private let work=DispatchQueue(label:"vibe.network")
    private let keyboard=Shortcut(),sink=AudioSink()
    private var socketFD:Int32 = -1
    private var localPort:UInt16=0,socketGeneration=0
    private var reader:DispatchSourceRead?,timer:DispatchSourceTimer?
    private var peer:sockaddr_in?,device="",token="",candidateToken=""
    private var ble:BLETransport?
    private var link:LinkKind?,wifiSeen=Date.distantPast,bleSeen=Date.distantPast
    private var clientID="",computerName="",protocolVersion=2
    private var claimWhenReady=false,claiming=false,authenticated=false
    private var hasRoute:Bool { peer != nil || ble?.ready==true }
    private var mayControl:Bool { hasRoute && !token.isEmpty && (protocolVersion<3 || state.owned) }
    private var lastSeen=Date.distantPast,lastAudio=Date.distantPast,lastBeacon=Date.distantPast,lastKey=Date.distantPast
    private var generation=0,sequence:UInt8?,active=false,ending=false
    private var pending:[String:(body:[String:Any],tries:Int,at:Date,kind:String)]=[:]
    private var state=ConnectionState()
    private var shortcut="ctrl+option+v"
    private var recentEvents:[Int]=[]
    private var revision=RevisionFence()
    private var bootID:Int? { revision.boot }
    private var lease:Int? { revision.lease }
    private var settingsRevision:UInt32?
    private var agentCapable=false
    private var slowHeartbeat=false,lastReadiness=""
    private var peerTimeout:TimeInterval { LANSocket.peerTimeout(capable:slowHeartbeat) }
    private var agentPending:[String:(body:[String:Any],expires:Date,at:Date,tries:Int)]=[:]
    var onAgentResult:((String)->Void)?
    var onDiagnostics:(([DiagnosticRecord])->Void)?
    private var diagnosticBoot:Int?,diagnosticAfter:UInt32=0,diagnosticWanted:UInt32=0,diagnosticRetry=Date.distantPast
    private func record(_ code:DiagnosticCode,numbers:[String:Int64]=[:]) {
        var row=DiagnosticRecord(code:code);row.link=link?.rawValue ?? "";row.numbers=numbers
        if let boot=bootID { row.numbers["boot"]=Int64(boot) }
        onDiagnostics?([row])
    }
    private lazy var radioRelay:RadioRelay = {
        let relay=RadioRelay(queue:work)
        relay.send={ [weak self] packet in
            guard let self=self,self.mayControl,!self.active else { return false }
            if self.link == .wifi,let peer=self.peer {
                return LANSocket.send(packet,from:self.socketFD,to:peer)
            }
            return self.link == .ble && self.ble?.send(packet,audio:true)==true
        }
        relay.onFailure={ [weak self] stream,record in
            guard let self=self,self.mayControl else { return }
            var row=record;row.link=self.link?.rawValue ?? "";if let boot=self.bootID { row.numbers["boot"]=Int64(boot) };self.onDiagnostics?([row])
            self.ble?.discardRadio();self.send(["type":"relay.error","stream":stream,"token":self.token])
        }
        return relay
    }()
    private func stopRadioRelay() { radioRelay.stop();ble?.discardRadio() }
    private var radioTransfer:RadioListTransfer?
    var onRadio:(([RadioStation],Int,Int)->Void)?
    var onRadioLoading:((Bool)->Void)?
    var onState:((ConnectionState)->Void)?
    var onSettings:((DeviceSettings)->Void)?
    var onResult:((Bool,String)->Void)?
    init(preview:Bool=false) {
        if preview { return }
        do { clientID=try PairingStore.clientID() } catch { state.error="无法保存电脑身份，请检查当前账户的文件权限" }
        computerName=PairingStore.computerName(Host.current().localizedName ?? ProcessInfo.processInfo.hostName)
        device=UserDefaults.standard.string(forKey:"device") ?? ""
        token=PairingStore.load(device) ?? ""
        shortcut=UserDefaults.standard.string(forKey:"shortcut") ?? "ctrl+option+v"
    }
    func start() { work.async { self.open() } }
    private func publish() {
        state.device=device;state.connected=hasRoute && Date().timeIntervalSince(lastSeen)<peerTimeout
        state.wifiConnected=peer != nil && Date().timeIntervalSince(wifiSeen)<peerTimeout
        state.bleConnected=ble?.ready==true
        state.paired = authenticated;state.recording=active;state.audio=AudioSink.deviceUID() != nil
        var copy=state
        DispatchQueue.main.async {
            copy.permission=Shortcut.permitted
            copy.message=copy.connectionTitle
            self.onState?(copy)
        }
    }
    private func open() {
        if ble==nil {
            let bluetooth=BLETransport(queue:work);ble=bluetooth;bluetooth.expectedDevice=device
            bluetooth.onStatus={ [weak self] phase,text in self?.state.bluetoothPhase=phase;self?.state.bluetooth=text;self?.publish() }
            bluetooth.onReady={ [weak self] found in
                guard let self=self else { return }
                if self.device.isEmpty { self.device=found;self.token=PairingStore.load(found) ?? "" }
                if self.link==nil { self.link = .ble }
                self.bleSeen=Date();self.beaconBLE()
            }
            bluetooth.onPacket={ [weak self] data in self?.receivePacket(data,via:.ble,from:nil);self?.publish() }
            bluetooth.onDisconnect={ [weak self] in self?.routeLost(.ble) }
        }
        // BLE discovery, heartbeats and request deadlines must keep running
        // even when opening the independent LAN socket fails.
        if timer==nil {
            let clock=DispatchSource.makeTimerSource(queue:work);clock.schedule(deadline:.now(),repeating:.milliseconds(500),leeway:.milliseconds(100))
            clock.setEventHandler { [weak self] in self?.tick() };timer=clock;clock.resume()
        }
        guard socketFD<0 else { return }
        let connection:LANSocket
        do { connection=try LANSocket.open() } catch { state.error=error.localizedDescription;publish();return }
        let fd=connection.descriptor;socketFD=fd;localPort=connection.port;state.error=""
        lastBeacon = .distantPast;socketGeneration+=1;let opened=socketGeneration
        let source=DispatchSource.makeReadSource(fileDescriptor:fd,queue:work)
        source.setEventHandler { [weak self] in guard let self=self,self.socketGeneration==opened else { return };self.receive() };source.setCancelHandler { Darwin.close(fd) };reader=source;source.resume()
        publish()
    }
    func retry() { refreshReadiness(resumeBluetooth:true) }
    // Window activation only refreshes readiness. It must not consume a BLE
    // takeover slot on a computer that deliberately parked its connection.
    func refreshReadiness(resumeBluetooth:Bool=false) { work.async {
        if resumeBluetooth { self.ble?.retry() }
        if self.socketFD<0 { self.open() }
        else { self.beacon(broadcast:true);self.lastBeacon=Date();self.publish() }
    } }
    private func send(_ body:[String:Any],type:UInt8=2,to target:sockaddr_in?=nil,via requested:LinkKind?=nil) {
        var object=body;object["client"]=clientID;object["name"]=computerName
        if let lease=lease { object["lease"]=lease }
        if let boot=bootID { object["boot"]=boot }
        guard !clientID.isEmpty,let data=try? JSONSerialization.data(withJSONObject:object),data.count<=1536 else { return }
        var packet=Data([type]);packet.append(data)
        if target != nil || (requested ?? link) == .wifi {
            guard let destination=target ?? peer else { return }
            LANSocket.send(packet,from:socketFD,to:destination)
        } else if ble?.send(packet) != true { log.warning("BLE send unavailable, bytes=\(packet.count)") }
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
        else if peer != nil { send(body,type:3,via:.wifi) }
        beaconBLE()
    }
    private func beaconBLE() {
        guard ble?.ready==true else { return }
        var body=LANSocket.beacon(port:localPort,token:token,audioReady:AudioSink.deviceUID() != nil,
            hotkeyReady:Shortcut.permitted,epoch:Int(Date().timeIntervalSince1970),tz:TimeZone.current.secondsFromGMT()/60)
        if token.isEmpty { body["type"]="discover" }
        send(body,via:.ble)
    }
    private func sendStatus() {
        if !token.isEmpty { send(["type":"pc.status","token":token,"audioReady":AudioSink.deviceUID() != nil,"hotkeyReady":Shortcut.permitted,"epoch":Int(Date().timeIntervalSince1970),"tz":TimeZone.current.secondsFromGMT()/60,"idleHeartbeat":LANSocket.negotiatedHeartbeatSeconds]) }
    }
    func notifyAgent(_ event:AgentEvent) {
        work.async {
            guard self.mayControl,Date().timeIntervalSince(self.lastSeen)<self.peerTimeout else { self.record(.agentDelivery,numbers:["detail":1]);DispatchQueue.main.async{self.onAgentResult?("设备离线，本次提醒未发送")};return }
            guard self.agentCapable else { self.record(.agentDelivery,numbers:["detail":2]);DispatchQueue.main.async{self.onAgentResult?("请先更新支持 Agent 提醒的设备固件")};return }
            guard self.agentPending.count<8 else { self.record(.agentDelivery,numbers:["detail":3]);return }
            var body=event.deviceBody;body["type"]="agent.notify";body["id"]=event.id;body["token"]=self.token
            body["ttl_ms"]=10000;body["expires"]=Date().timeIntervalSince1970+10
            self.agentPending[event.id]=(body,Date().addingTimeInterval(5),Date(),1);self.send(body)
        }
    }
    private func tick() {
        let now=Date()
        ble?.tick()
        if peer != nil,now.timeIntervalSince(wifiSeen)>peerTimeout { routeLost(.wifi) }
        if !hasRoute,now.timeIntervalSince(lastSeen)>peerTimeout { state.owned=false }

        if active && (now.timeIntervalSince(lastAudio)>3 || now.timeIntervalSince(lastKey)>180 || !Shortcut.permitted) {
            if !ending && now.timeIntervalSince(lastAudio)>3 { record(.voiceTimeout) };endVoice(immediate:true)
        }
        let readiness="\(AudioSink.deviceUID() != nil)/\(Shortcut.permitted)/\(TimeZone.current.secondsFromGMT())"
        let changed=readiness != lastReadiness;lastReadiness=readiness
        if changed { state.voiceError="" }
        if now.timeIntervalSince(lastBeacon)>((!hasRoute || token.isEmpty) ? 2 : LANSocket.heartbeat(capable:slowHeartbeat)) || (changed && hasRoute) {
            beacon(broadcast:peer == nil);lastBeacon=now
        }
        // An idle BLE peripheral may skip connection events. Leave time for a
        // complete request/reply before retrying instead of flooding its queue.
        for (id,entry) in pending where now.timeIntervalSince(entry.at)>(link == .ble ? 3.0 : 1.2) {
            let waitingForSwitch=entry.kind=="wireless.set" && !mayControl
            if waitingForSwitch,now.timeIntervalSince(entry.at)<12 { continue }
            if entry.tries>=4 || waitingForSwitch {
                pending.removeValue(forKey:id)
                if entry.kind=="diagnostics.get" { state.diagnosticsLoading=false;state.diagnosticsError="设备诊断读取超时，已保存的记录不受影响。";diagnosticRetry=now.addingTimeInterval(30);continue }
                record(.requestTimeout,numbers:["operation":entry.kind.hasPrefix("radio.") ? 2 : entry.kind.hasPrefix("power.") ? 3 : 1])
                if entry.kind=="peer.claim" { claiming=false };if entry.kind.hasPrefix("power.") { state.powerLoading=false;state.powerError="读取超时，请重试；下方保留上次读取结果。" } else if entry.kind.hasPrefix("radio.") { cancelRadio("电台同步超时，原列表已保留，请重新读取") } else { DispatchQueue.main.async { self.onResult?(false,"设备没有响应，请检查连接后重试") } }
            }
            else { send(entry.body);pending[id]=(entry.body,entry.tries+1,now,entry.kind) }
        }
        for (id,entry) in agentPending where now.timeIntervalSince(entry.at)>0.8 {
            if now>entry.expires || entry.tries>=4 { agentPending.removeValue(forKey:id);record(.agentDelivery,numbers:["detail":4]);DispatchQueue.main.async{self.onAgentResult?("设备未确认提醒，请检查连接")} }
            else { send(entry.body);agentPending[id]=(entry.body,entry.expires,now,entry.tries+1) }
        }
        syncDiagnostics();publish()
    }
    private func receive() {
        var bytes=[UInt8](repeating:0,count:2048)
        for _ in 0..<32 {
            var from=sockaddr_in();var size=socklen_t(MemoryLayout<sockaddr_in>.size)
            let n=withUnsafeMutablePointer(to:&from) { p in p.withMemoryRebound(to:sockaddr.self,capacity:1) { Darwin.recvfrom(socketFD,&bytes,bytes.count,0,$0,&size) } }
            if n<=0 { break }
            let data=Data(bytes.prefix(n))
            guard from.sin_family==sa_family_t(AF_INET),from.sin_port==LANSocket.discoveryPort.bigEndian else { continue }
            receivePacket(data,via:.wifi,from:from)
        }
        publish()
    }
    private func receivePacket(_ data:Data,via source:LinkKind,from:sockaddr_in?) {
        guard let packet=Packet.parse(data) else { return }
        if case .event(let body)=packet,body["event"] as? String=="device.hello",let found=body["device"] as? String {
            guard found.count==12,found.allSatisfy({"0123456789abcdef".contains($0)}),device.isEmpty || found==device else { return }
            guard ConnectionRevision.validHello(body,requireCurrentProtocol:protocolVersion>=3 && bootID != nil) else { return }
            let previousLease=lease
            let previouslyOwned=state.owned
            let acceptance=revision.accept(boot:body["boot"] as? Int,lease:body["lease"] as? Int)
            guard acceptance != .stale else { return }
            let rebooted=acceptance == .restarted
            if rebooted { record(.deviceRestart);disconnect("设备已重启，未保存的修改已保留") }
            let reconnect = !state.connected
            device=found;ble?.expectedDevice=found
            if token.isEmpty { token=PairingStore.load(found) ?? "" }
            if source == .wifi { peer=from;wifiSeen=Date() } else { bleSeen=Date() }
            protocolVersion=body["proto"] as? Int ?? 2
            agentCapable=body["agentNotify"] as? Int==1;slowHeartbeat=body["idleHeartbeat"] as? Int==30
            state.powerAvailable=body["powerDiagnostics"] as? Int==1
            state.diagnosticsAvailable=body["diagnostics"] as? Int==1
            if diagnosticBoot != bootID { diagnosticBoot=bootID;diagnosticAfter=0;diagnosticWanted=0;diagnosticRetry = .distantPast }
            if let seq=body["diagnosticSeq"] as? UInt32 { diagnosticWanted=max(diagnosticWanted,seq) }
            if rebooted || bootID==nil { recentEvents=[];settingsRevision=nil }
            let paired=body["paired"] as? Bool==true;authenticated=paired
            state.owned=protocolVersion<3 ? paired : body["owned"] as? Bool==true
            state.ownerName=body["ownerName"] as? String ?? ""
            state.wifiEnabled=body["wifiEnabled"] as? Bool ?? true;state.bleEnabled=body["bleEnabled"] as? Bool ?? true
            ble?.setDeviceEnabled(state.bleEnabled)
            let confirmed=pending.filter { $0.value.kind=="wireless.set" && WirelessConfirmation.matches(request:$0.value.body,hello:body) }.map(\.key)
            for id in confirmed { pending.removeValue(forKey:id) }
            if !confirmed.isEmpty { state.error="";DispatchQueue.main.async { self.onResult?(true,"无线连接已更新") } }
            // The device announces the new lease to all computers before its
            // request-specific reply. That first authoritative owner status
            // already confirms takeover; do not cancel it as a failed save.
            if paired,state.owned {
                let claimed=pending.filter{$0.value.kind=="peer.claim"}.map(\.key)
                for id in claimed { pending.removeValue(forKey:id) }
                if !claimed.isEmpty { claiming=false;state.error="";DispatchQueue.main.async { self.onResult?(true,"已切换到本机") } }
            }
            if !paired,!token.isEmpty {
                endVoice(immediate:true);state.owned=false;state.error="请在设备设置中打开配对电脑，输入配对码"
            }
            if let id=body["id"] as? String,let entry=pending.removeValue(forKey:id) {
                if entry.kind=="peer.claim" { claiming=false }
                if body["ok"] as? Bool==true,entry.kind=="pair" {
                    if PairingStore.save(candidateToken,device:device) { token=candidateToken;candidateToken="";UserDefaults.standard.set(device,forKey:"device");state.error="";sendStatus() }
                    else { state.error="无法保存本地配对信息" }
                } else if body["ok"] as? Bool==false { state.error=body["error"] as? String ?? "设备拒绝了操作" }
                let ok=body["ok"] as? Bool==true
                let message=ok ? (entry.kind=="wireless.set" ? "无线连接已更新" : "") : state.error
                if ok { state.error="" }
                DispatchQueue.main.async { self.onResult?(ok,message) }
            }
            if state.owned,let chosen=LinkKind(rawValue:body["transport"] as? String ?? ""),chosen == .wifi ? peer != nil : ble?.ready==true { link=chosen }
            else if link==nil { link=source }
            if previousLease != nil,previousLease != lease || (previouslyOwned && !state.owned) {
                endVoice(immediate:true);cancelRequests("使用权或链路已改变，请重新读取后保存")
            }
            if let peer=peer {
                var ip=peer.sin_addr;var text=[CChar](repeating:0,count:Int(INET_ADDRSTRLEN));inet_ntop(AF_INET,&ip,&text,socklen_t(text.count));state.address=String(cString:text)
            } else { state.address="蓝牙" }
            state.transport=link == .wifi ? "Wi-Fi" : "蓝牙";lastSeen=Date()
            if claimWhenReady,paired { claimWhenReady=false;if !state.owned { claiming=true;request("peer.claim",extra:[:]) } }
            if !state.owned,!claiming,paired,!state.ownerName.isEmpty,source == .ble { ble?.park() }
            let revision=(body["settingsRevision"] as? NSNumber)?.uint32Value
            if (reconnect || (revision != nil && revision != settingsRevision)),mayControl,pending.isEmpty { request("config.get",extra:[:]) }
            syncDiagnostics()
            return
        }
        guard source==link,hasRoute else { return }
        if source == .wifi {
            guard let selected=peer,let from=from,LANSocket.sameEndpoint(selected,from) else { return };wifiSeen=Date()
        } else { bleSeen=Date() }
        lastSeen=Date()
        switch packet {
        case .ping: break
        case .audio(let seq,let pcm):
            guard active,mayControl else { return }
            if let previous=sequence {
                let delta=seq &- previous;if delta==0 || delta>128 { return }
                if delta>1 { state.dropped+=Int(delta)-1;for _ in 0..<min(3,Int(delta)-1) { sink.append([Int16](repeating:0,count:1600)) } }
            }
            sequence=seq;state.packets+=1;lastAudio=Date();sink.append(pcm)
        case .event(let body):
            let kind=body["event"] as? String ?? "unknown"
            log.debug("Event \(kind,privacy:.public), link=\(source.rawValue,privacy:.public), bytes=\(data.count)")
            if protocolVersion>=3 {
                guard ConnectionRevision.matchesEvent(body,boot:bootID,lease:lease) else { return }
            } else {
                if let eventBoot=body["boot"] as? Int,eventBoot != bootID { return }
                if let eventLease=body["lease"] as? Int,eventLease != lease { return }
            }
            guard mayControl else { return };handle(body)
        }
    }
    private func routeLost(_ source:LinkKind) {
        if link==source && state.owned { record(.linkLost) }
        if source == .wifi { peer=nil;wifiSeen = .distantPast } else { bleSeen = .distantPast }
        if link==source {
            stopRadioRelay()
            endVoice(immediate:true);cancelRequests("链路已断开，未保存的修改已保留",preserveWireless:true)
            link=peer != nil ? .wifi : ble?.ready==true ? .ble : nil
            state.owned=false;lastBeacon = .distantPast
        }
        publish()
    }
    private func handle(_ body:[String:Any]) {
        if let id=body["eid"] as? Int {
            if recentEvents.contains(id) { return }
            recentEvents.append(id);if recentEvents.count>64 { recentEvents.removeFirst() }
        }
        switch body["event"] as? String ?? "" {
        case "diagnostics.available":
            if let seq=body["sequence"] as? UInt32 { diagnosticWanted=max(diagnosticWanted,seq);syncDiagnostics() }
        case "diagnostics.reply":
            guard let id=body["id"] as? String,let entry=pending[id],entry.kind=="diagnostics.get",let boot=bootID,
                  let after=entry.body["after"] as? UInt32,
                  let rows=DiagnosticRecord.deviceRows(body,device:device,boot:boot,after:after) else { return }
            pending.removeValue(forKey:id);state.diagnosticsLoading=false;state.diagnosticsError=""
            onDiagnostics?(rows)
            if let seq=rows.compactMap({$0.numbers["sequence"]}).max() { diagnosticAfter=UInt32(seq) }
            else if let newest=body["sequence"] as? UInt32 { diagnosticAfter=newest }
            syncDiagnostics()
        case "relay.open": if !active { radioRelay.open(body) }
        case "relay.credit": radioRelay.update(body)
        case "relay.close": radioRelay.close(body);ble?.discardRadio()
        case "agent.reply":
            guard let id=body["id"] as? String,agentPending.removeValue(forKey:id) != nil else { return }
            let ok=body["ok"] as? Bool==true
            DispatchQueue.main.async{self.onAgentResult?(ok ? "设备已接收提醒" : "设备未接受提醒")}
        case "voice.start":
            stopRadioRelay()
            if active && ending { endVoice(immediate:true) }
            guard !token.isEmpty,!active else { return }
            do { try sink.start();try keyboard.start(shortcut);active=true;ending=false;generation+=1;sequence=nil;lastAudio=Date();lastKey=Date();state.voiceError="" }
            catch { sink.stop();keyboard.stop();state.voiceError=error.localizedDescription;record(.voiceStart,numbers:["operation":!Shortcut.permitted ? 1 : AudioSink.deviceUID()==nil ? 2 : 3]) }
        case "voice.end": endVoice(immediate:false)
        case "key.action":
            if !token.isEmpty,!active,body["action"] as? String=="enter" { keyboard.enter() }
        case "radio.reply":
            guard let id=body["id"] as? String,let entry=pending[id],entry.kind.hasPrefix("radio."),var transfer=radioTransfer else { return }
            pending.removeValue(forKey:id)
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
        log.debug("Request \(type,privacy:.public), link=\(self.link?.rawValue ?? "none",privacy:.public)")
        let id=UUID().uuidString
        var body=extra;body["type"]=type;body["id"]=id;body["token"]=type=="pair" ? candidateToken : token
        pending[id]=(body,1,Date(),type);send(body)
    }
    func pair(_ code:String) {
        work.async {
            guard self.hasRoute,code.count==6,code.allSatisfy({ $0.isNumber }) else { DispatchQueue.main.async { self.onResult?(false,"请先找到设备，并输入屏幕上的六位配对码") };return }
            if self.candidateToken.isEmpty { self.candidateToken=PairingStore.random() }
            self.request("pair",extra:["code":code])
        }
    }
    func save(_ prefs:DeviceSettings,changes:[String:Any]) { work.async { guard prefs.validForSave,self.mayControl else { return };self.request("config.set",extra:["settings":changes]) } }
    func reload() { work.async { if self.mayControl { self.request("config.get",extra:[:]) } } }
    func readPower(reset:Bool=false) {
        work.async {
            guard self.mayControl,self.state.powerAvailable,!self.state.powerLoading else { return }
            self.state.powerLoading=true;self.state.powerError="";self.request(reset ? "power.reset" : "power.get",extra:[:]);self.publish()
        }
    }
    private func syncDiagnostics(force:Bool=false) {
        guard mayControl,state.diagnosticsAvailable,!active,!state.diagnosticsLoading,Date()>=diagnosticRetry,
              force || diagnosticWanted>diagnosticAfter else { return }
        state.diagnosticsLoading=true;state.diagnosticsError="";request("diagnostics.get",extra:["after":diagnosticAfter])
    }
    func readDiagnostics() { work.async { self.diagnosticRetry = .distantPast;self.syncDiagnostics(force:true);self.publish() } }
    private func sendRadioStep() {
        guard let transfer=radioTransfer else { return }
        let (type,extra)=transfer.command;request(type,extra:extra)
    }
    private func beginRadioRead() {
        guard mayControl,radioTransfer==nil else { return }
        radioTransfer=RadioListTransfer();DispatchQueue.main.async { self.onRadioLoading?(true) };sendRadioStep()
    }
    private func cancelRadio(_ message:String) {
        guard let transfer=radioTransfer else { return }
        if transfer.writing { send(["type":"radio.abort","transaction":transfer.transaction,"token":token]) }
        radioTransfer=nil;pending=pending.filter { !$0.value.kind.hasPrefix("radio.") }
        DispatchQueue.main.async { self.onRadioLoading?(false);self.onResult?(false,message) }
    }
    private func cancelRequests(_ message:String,preserveWireless:Bool=false) {
        stopRadioRelay()
        let interrupted=pending.values.contains { !$0.kind.hasPrefix("radio.") && !$0.kind.hasPrefix("power.") && !$0.kind.hasPrefix("diagnostics.") && !(preserveWireless && $0.kind=="wireless.set") }
        cancelRadio(message)
        // Disabling the current radio may drop its ACK. Retain only that
        // intent until an authenticated owner HELLO confirms the switch.
        // Other old-route controls must never be replayed on the new route.
        pending=preserveWireless ? pending.filter{$0.value.kind=="wireless.set"} : [:]
        agentPending.removeAll();claiming=false
        if state.powerLoading { state.powerLoading=false;state.powerError=message }
        if state.diagnosticsLoading { state.diagnosticsLoading=false;state.diagnosticsError="连接中断，稍后会补取设备记录。" }
        if interrupted { DispatchQueue.main.async { self.onResult?(false,message) } }
    }
    func reloadRadio() { work.async { self.beginRadioRead() } }
    func saveRadio(_ stations:[RadioStation],revision:Int,capacity:Int) {
        work.async {
            guard self.mayControl,!self.active,self.radioTransfer==nil else { DispatchQueue.main.async { self.onResult?(false,"请等待设备连接及电台读取完成，并结束语音输入") };return }
            do { self.radioTransfer=try RadioListTransfer(stations:stations,revision:revision,capacity:capacity);DispatchQueue.main.async { self.onRadioLoading?(true) };self.sendRadioStep() }
            catch { DispatchQueue.main.async { self.onResult?(false,error.localizedDescription) } }
        }
    }
    func claim() { work.async {
        if self.hasRoute { self.claiming=true;self.request("peer.claim",extra:[:]) }
        else { self.claimWhenReady=true;self.ble?.retry();self.beacon(broadcast:true) }
    } }
    func setWireless(wifi:Bool,bluetooth:Bool) { work.async {
        guard self.mayControl else { return };self.request("wireless.set",extra:["wifiEnabled":wifi,"bleEnabled":bluetooth])
    } }
    func setShortcut(_ spec:String) throws {
        _=try Shortcut.resolve(spec);UserDefaults.standard.set(spec,forKey:"shortcut")
        work.async { self.endVoice(immediate:true);self.shortcut=spec }
    }
    private func disconnect(_ message:String) {
        stopRadioRelay()
        slowHeartbeat=false;state.powerAvailable=false;state.powerLoading=false
        state.diagnosticsAvailable=false;state.diagnosticsLoading=false
        agentPending.removeAll();agentCapable=false;
        cancelRadio(message)
        let interrupted = !pending.isEmpty
        pending.removeAll();peer=nil;link=nil;state.owned=false;authenticated=false;state.address="";lastSeen = .distantPast;lastBeacon = .distantPast
        settingsRevision=nil;recentEvents.removeAll();endVoice(immediate:true)
        if interrupted { DispatchQueue.main.async { self.onResult?(false,message) } }
    }
    func stop(completion:(()->Void)?=nil) {
        work.sync {
            if !token.isEmpty { send(["type":"pc.status","token":token,"audioReady":false,"hotkeyReady":false]) }
            disconnect("连接已结束，未确认的修改请重新读取");
            ble?.stop();ble=nil;keyboard.stop();sink.stop();timer?.cancel();timer=nil;socketGeneration+=1
            if let reader=reader {
                let fd=socketFD
                reader.setCancelHandler { Darwin.close(fd);if let completion=completion { DispatchQueue.main.async(execute:completion) } }
                reader.cancel()
            } else if let completion=completion { DispatchQueue.main.async(execute:completion) }
            reader=nil;socketFD = -1;localPort=0
        }
    }
}

// Keep this harness beside the private dispatcher: it exercises real packet
// acceptance and cancellation without exposing mutation hooks in production.
extension Engine {
    static func testConnectionDispatch(_ check:(Bool,String)->Void) {
        let engine=Engine(preview:true)
        engine.clientID=String(repeating:"1",count:32);engine.token=String(repeating:"a",count:64)
        engine.device="ffffffffffff"
        func endpoint(_ port:UInt16)->sockaddr_in {
            var address=sockaddr_in();address.sin_len=UInt8(MemoryLayout<sockaddr_in>.size)
            address.sin_family=sa_family_t(AF_INET);address.sin_addr.s_addr=inet_addr("127.0.0.1");address.sin_port=port.bigEndian
            return address
        }
        func hello(_ boot:Int,_ lease:Int,_ owned:Bool=true)->[String:Any] {
            ["event":"device.hello","proto":3,"device":"ffffffffffff","boot":boot,"lease":lease,
             "paired":true,"owned":owned,"ownerName":owned ? "This computer":"Other computer","transport":"wifi"]
        }
        func inject(_ body:[String:Any],from:sockaddr_in) {
            var data=Data([2]);data.append(try! JSONSerialization.data(withJSONObject:body))
            engine.receivePacket(data,via:.wifi,from:from)
        }
        do {
            let socket=try LANSocket.open(),server=try LANSocket.open()
            defer { engine.socketFD = -1;Darwin.close(socket.descriptor);Darwin.close(server.descriptor) }
            engine.socketFD=socket.descriptor
            let peer=endpoint(server.port)
            inject(hello(100,10),from:peer)
            check(engine.state.owned && engine.bootID==100 && engine.lease==10,"dispatcher accepts initial owner")
            var ready=pollfd(fd:server.descriptor,events:Int16(POLLIN),revents:0)
            check(Darwin.poll(&ready,1,1000)>0,"real dispatcher sends initial request")
            var bytes=[UInt8](repeating:0,count:2048)
            let count=Darwin.recv(server.descriptor,&bytes,bytes.count,0)
            let request=count>1 ? try? JSONSerialization.jsonObject(with:Data(bytes[1..<count])) as? [String:Any] : nil
            check(request?["boot"] as? Int==100 && request?["lease"] as? Int==10,"wire control carries both boot nonce and lease")
            let oldRequests=Set(engine.pending.keys)
            var invalid=hello(101,11);invalid.removeValue(forKey:"lease")
            let differentPort=server.port==UInt16.max ? 1 : server.port+1
            inject(invalid,from:endpoint(differentPort))
            check(engine.bootID==100 && engine.lease==10 && engine.peer?.sin_port==peer.sin_port,"malformed hello cannot replace endpoint or erase fence")
            var stranger=hello(200,1);stranger["device"]="eeeeeeeeeeee"
            inject(stranger,from:endpoint(differentPort))
            check(engine.bootID==100 && engine.lease==10,"another physical device cannot take over the remembered identity")
            inject(hello(100,11,false),from:peer)
            check(!engine.state.owned && oldRequests.isDisjoint(with:engine.pending.keys),"handoff cancels old-owner pending work")
            inject(hello(100,10,true),from:peer)
            check(!engine.state.owned && engine.lease==11,"delayed owner hello cannot undo handoff")
            engine.claiming=true;engine.request("peer.claim",extra:[:])
            let claim=engine.pending.first{$0.value.kind=="peer.claim"}!.key
            inject(hello(100,12),from:peer) // Unsolicited lease announcement arrives first.
            check(engine.state.owned && !engine.claiming && engine.pending[claim]==nil,"unsolicited owner announcement confirms takeover instead of reporting cancellation")
            var confirmed=hello(100,12);confirmed["id"]=claim;confirmed["ok"]=true;inject(confirmed,from:peer)
            check(engine.state.owned && engine.lease==12,"late request-specific takeover reply stays idempotent")
            let change:[String:Any]=["event":"config.changed","boot":100,"lease":11,"settingsRevision":90,"settings":DeviceSettings().object]
            inject(change,from:peer)
            check(engine.settingsRevision==nil,"old-owner settings do not update the current computer")
            var accepted=change;accepted["lease"]=12;inject(accepted,from:peer)
            check(engine.settingsRevision==90,"current owner's settings are accepted")
            let beforeRestart=Set(engine.pending.keys)
            inject(hello(101,1),from:peer)
            check(engine.bootID==101 && engine.lease==1 && beforeRestart.isDisjoint(with:engine.pending.keys),"device restart clears pending operations even when lease resets")
            var delayed=change;delayed["lease"]=1;delayed["settingsRevision"]=91;inject(delayed,from:peer)
            check(engine.settingsRevision==nil,"previous boot cannot inject settings with the new boot's matching lease")
            inject(hello(100,13),from:peer)
            check(engine.bootID==101,"retired boot cannot reappear after reconnect")
            inject(["event":"device.hello","device":"ffffffffffff","proto":2,"owned":true,"paired":true],from:peer)
            check(engine.bootID==101,"legacy downgrade cannot discard active revision protection")
            engine.pending.removeAll()
            engine.request("wireless.set",extra:["wifiEnabled":true,"bleEnabled":false])
            engine.request("config.set",extra:["settings":["volume":55]])
            let switchID=engine.pending.first{$0.value.kind=="wireless.set"}!.key
            engine.cancelRequests("test route loss",preserveWireless:true)
            check(engine.pending.count==1 && engine.pending[switchID] != nil,"route loss retains only pending radio-switch confirmation, never old configuration writes")
            engine.state.owned=false
            var switched=hello(101,2);switched["wifiEnabled"]=true;switched["bleEnabled"]=false
            inject(switched,from:peer)
            check(engine.state.owned && engine.pending[switchID]==nil,"new route confirms wireless switch after the old route disappears before its ACK")
            print("Connection dispatcher: identity, real wire revision, takeover, stale owner/settings, restart and downgrade PASS")
        } catch { check(false,"connection dispatcher harness: \(error.localizedDescription)") }
    }
}
