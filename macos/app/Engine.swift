import Foundation
import Darwin
import CryptoKit
import os
struct ConnectionState {
    var message="正在查找设备…",device="",address="",paired=false,connected=false,recording=false
    var devices:[DiscoveredDevice]=[],canPair=false,paused=false
    var transport=ConnectionTransport.wifi,bluetoothStatus=""
    var bluetoothLinkActive=false,connectionRequestActive=false,waitingForConfirmation=false,wifiConnected=false,bleConnected=false
    var bluetoothPairingRepairRequired=false
    var permission=false,audio=false,packets=0,dropped=0,error=""
    var powerAvailable=false,powerLoading=false,powerError=""
    var powerSnapshot:PowerSnapshot?
}

final class Engine {
    private let voiceLog=Logger(subsystem:"io.github.vibecompanion.mac",category:"voice")
    private var voiceStarted=Date.distantPast,voiceInitialPackets=0,voiceInitialDropped=0,voiceMaxGap=0.0
    private let work=DispatchQueue(label:"vibe.network")
    private let lanEnabled:Bool
    private let keyboard=Shortcut(),sink=AudioSink()
    private var socketFD:Int32 = -1
    private var localPort:UInt16=0,socketGeneration=0
    private var reader:DispatchSourceRead?,timer:DispatchSourceTimer?
    private var peer:sockaddr_in?,device="",token="",candidateToken=""
    private var discovery=DeviceDiscovery(),lastDiscovery=Date.distantPast
    private var clientID=""
    private var bluetooth:BluetoothLink?
    private var transport=ConnectionTransport.wifi
    private var authenticated=false,paused=false,pairing=false
    private var connectionRequest="",requestStarted=Date.distantPast,lastRequest=Date.distantPast,connectIntent=false
    private var requestDelivered=false
    private var wifiLastSeen=Date.distantPast,bleLastSeen=Date.distantPast
    private var connectionMessage="",searchStarted=Date()
    private var lastSeen=Date.distantPast,lastAudio=Date.distantPast,lastBeacon=Date.distantPast,lastKey=Date.distantPast
    private var generation=0,sequence:UInt8?,active=false,ending=false
    private var pending:[String:(body:[String:Any],tries:Int,at:Date,kind:String)]=[:]
    private var state=ConnectionState()
    private var shortcut="ctrl+option+v"
    private var recentEvents:[Int]=[]
    private var bootID:Int?
    private var settingsRevision:UInt32?
    private var weatherCapable=false,weatherJob:WeatherFetch?,weatherLocation:WeatherLocation?,weatherGeneration=0,weatherHasAir=false
    private var nextWeather=Date.distantPast
    private var agentCapable=false
    private var slowHeartbeat=false,lastReadiness=""
    private var peerTimeout:TimeInterval { ConnectionTiming.sessionTimeout }
    private var agentPending:[String:(body:[String:Any],expires:Date,at:Date,tries:Int)]=[:]
    private var agentNoticeKeys:[String:String]=[:]
    private var agentNoticeOrder:[String]=[]
    var onAgentResult:((String)->Void)?
    var onAgentDismiss:((String)->Void)?
    private var radioPlayback:RadioPlayback?
    private var radioDelivery:RadioDelivery?
    private var radioActivity:NSObjectProtocol?
    private var radioProgressAt=Date.distantPast
    private var radioTickAt=Date.distantPast,radioMaxTickGap=0.0
    private var radioClock:DispatchSourceTimer?
    private var radioFailureSent=false
    private var radioReportAt=Date.distantPast
    private var radioTransfer:RadioListTransfer?
    var onRadio:(([RadioStation],Int,Int,String)->Void)?
    var onRadioLoading:((Bool)->Void)?
    var onRadioVerified:((String)->Void)?
    var onState:((ConnectionState)->Void)?
    var onSettings:((DeviceSettings,String)->Void)?
    var onResult:((Bool,String)->Void)?
    init(preview:Bool=false,lanEnabled:Bool=true) {
        self.lanEnabled=lanEnabled
        if preview { return }
        clientID=ComputerIdentity.load()
        device=UserDefaults.standard.string(forKey:"device") ?? ""
        transport=ConnectionTransport(rawValue:UserDefaults.standard.string(forKey:"connectionTransport") ?? "wifi") ?? .wifi
        token=PairingStore.load(device) ?? ""
        shortcut=UserDefaults.standard.string(forKey:"shortcut") ?? "ctrl+option+v"
    }
    func start() { work.async {
        let ble=BluetoothLink(queue:self.work);self.bluetooth=ble
        ble.onFound={ [weak self,weak ble] id,identifier,connectable in
            guard let self=self else { return }
            self.discovery.foundBLE(id,identifier:identifier,connectable:connectable)
            if id==self.device,!self.paused,(!self.token.isEmpty || !self.connectionRequest.isEmpty) {
                if connectable { ble?.connect(id) }

            }
            self.publish()
        }
        ble.onPacket={ [weak self] id,data in
            guard let self=self,id==self.device else { return }
            self.processPacket(data,from:Self.bleEndpoint,viaBLE:true);self.publish()
        }
        ble.onReady={ [weak self] in
            guard let self=self,!self.paused else { return }
            if self.peer==nil { self.peer=Self.bleEndpoint }
            if !self.token.isEmpty { self.beacon(broadcast:false) }
            self.sendConnectionRequest();self.lastBeacon=Date();self.publish()
        }
        ble.onDisconnected={ [weak self] message in
            guard let self=self else { return }
            self.bleLastSeen = .distantPast;self.updateTransport()
            if !self.paused && Date().timeIntervalSince(self.wifiLastSeen)>ConnectionTiming.linkFresh { self.connectionMessage=message }
            self.publish()
        }
        ble.onAuthenticationFailed={ [weak self] message in
            guard let self=self else { return }
            self.bleLastSeen = .distantPast
            if Date().timeIntervalSince(self.wifiLastSeen)>ConnectionTiming.linkFresh { self.connectionMessage=message }
            self.publish()
        }
        ble.onStatus={ [weak self] status in
            guard let self=self else { return };self.state.bluetoothStatus=status
            if !self.paused && !self.authenticated && self.connectionRequest.isEmpty { self.connectionMessage=status }
            self.publish()
        }
        ble.onPairingRepairChanged={ [weak self] id,required in self?.setBluetoothPairingRepair(id,required:required) }
        self.startTimer();self.open()
    } }
    private static var bleEndpoint:sockaddr_in {
        var address=sockaddr_in();address.sin_family=sa_family_t(AF_INET);address.sin_port=LANSocket.discoveryPort.bigEndian
        address.sin_addr.s_addr=inet_addr("127.0.0.1");return address
    }
    private func publish() {
        state.device=device;state.connected=authenticated && peer != nil && Date().timeIntervalSince(lastSeen)<peerTimeout
        state.devices=discovery.visible();state.paused=paused;state.transport=transport;state.bluetoothLinkActive=bluetooth?.isActive ?? false
        state.canPair=false;state.connectionRequestActive = !connectionRequest.isEmpty
        state.waitingForConfirmation = state.connectionRequestActive && requestDelivered
        state.wifiConnected=authenticated && Date().timeIntervalSince(wifiLastSeen)<ConnectionTiming.linkFresh
        state.bleConnected=authenticated && Date().timeIntervalSince(bleLastSeen)<ConnectionTiming.linkFresh
        state.paired = !token.isEmpty && authenticated;state.recording=active;state.audio=AudioSink.deviceUID() != nil
        var copy=state
        let needsConnection=token.isEmpty,paused=self.paused,message=connectionMessage,selected=device,searchAge=Date().timeIntervalSince(searchStarted)
        let targetAvailable=state.devices.contains(where:{$0.id==device})
        DispatchQueue.main.async {
            copy.permission=Shortcut.permitted
            copy.message=copy.recording ? "正在语音输入" : copy.connected ? "已连接" : !message.isEmpty ? message : paused ? "已断开" : selected.isEmpty ? (copy.devices.isEmpty ? (searchAge>10 ? "未发现设备" : "正在查找设备…") : "请选择设备") : !targetAvailable && searchAge>10 ? "未发现所选设备" : !copy.paired && !copy.waitingForConfirmation && needsConnection ? "点击设备连接" : "正在连接…"
            self.onState?(copy)
        }
    }
    private func startTimer() {
        guard timer==nil else { return }
        let clock=DispatchSource.makeTimerSource(queue:work)
        clock.schedule(deadline:.now(),repeating:.milliseconds(500),leeway:.milliseconds(100))
        clock.setEventHandler { [weak self] in self?.tick() };timer=clock;clock.resume()
    }
    private func open() {
        startTimer()
        guard lanEnabled else { publish();return }
        guard socketFD<0 else { return }
        let connection:LANSocket
        do { connection=try LANSocket.open() } catch { state.error=error.localizedDescription;publish();return }
        let fd=connection.descriptor;socketFD=fd;localPort=connection.port;state.error=""
        lastBeacon = .distantPast;socketGeneration+=1;let opened=socketGeneration
        let source=DispatchSource.makeReadSource(fileDescriptor:fd,queue:work)
        source.setEventHandler { [weak self] in guard let self=self,self.socketGeneration==opened else { return };self.receive() };source.setCancelHandler { Darwin.close(fd) };reader=source;source.resume()
        publish()
    }
    func retry(resume:Bool=true) { work.async { self.retryNow(resume:resume) } }
    private func retryNow(resume:Bool) {
        guard resume || !paused else { publish();return }
        paused=false;connectionMessage="";state.error="";searchStarted=Date();lastDiscovery = .distantPast;lastBeacon = .distantPast
        if socketFD<0 { open() }
        bluetooth?.scan()
        if !device.isEmpty && !token.isEmpty { bluetooth?.connect(device,resetBackoff:resume) }
        beacon(broadcast:true)
        if peer != nil && !token.isEmpty { beacon(broadcast:false) }
        publish()
    }
    func selectDevice(_ id:String,via preference:ConnectionTransport?=nil) { work.async {
        self.selectDeviceNow(id)
    } }
    private func setBluetoothPairingRepair(_ id:String,required:Bool) {
        guard id==device else { return };state.bluetoothPairingRepairRequired=required;publish()
    }
    func retryBluetooth() { work.async {
        guard !self.device.isEmpty else { return }
        self.paused=false
        if !self.authenticated { self.connectIntent=true;self.connectionMessage="正在连接蓝牙…" }
        if !self.authenticated && self.connectionRequest.isEmpty && self.token.isEmpty { self.beginConnectionRequest() }
        self.bluetooth?.scan();self.bluetooth?.connect(self.device,resetBackoff:true);self.publish()
    } }
    private func selectDeviceNow(_ id:String) {
        guard let found=discovery.devices[id] else { return }
        cancelConnectionRequest()
        if authenticated { send(["type":"pc.disconnect","token":token]) }
        bluetooth?.disconnect();disconnect("已切换设备");device=id;token=PairingStore.load(id) ?? ""
        transport=found.hasWiFi ? .wifi : .bluetooth
        state.powerSnapshot=nil;state.bluetoothPairingRepairRequired=false;state.bluetoothStatus="";candidateToken="";peer=found.hasWiFi ? found.endpoint : Self.bleEndpoint
        paused=false;connectionMessage="";connectIntent=true
        if found.hasBLE { bluetooth?.connect(id,resetBackoff:true) }
        if token.isEmpty { beginConnectionRequest() } else { beacon(broadcast:false) }
        publish()
    }
    func cancelConnection() { work.async {
        self.cancelConnectionRequest();self.paused=true;self.bluetooth?.disconnect();self.disconnect("已取消连接");self.connectionMessage="已取消连接";self.publish()
    } }
    private func cancelConnectionRequest() {
        if !connectionRequest.isEmpty { sendOnAvailableLinks(connectionBody(type:"connection.cancel")) }
        connectionRequest="";requestDelivered=false;pairing=false;candidateToken="";connectIntent=false
    }
    private func beginConnectionRequest() {
        guard connectionRequest.isEmpty else { return }
        token="";candidateToken=PairingStore.random();connectionRequest=UUID().uuidString.lowercased()
        requestDelivered=false;requestStarted=Date();lastRequest = .distantPast;pairing=true;connectionMessage="正在请求连接…";sendConnectionRequest()
    }
    private func connectionBody(type:String)->[String:Any] {
        ["type":type,"device":device,"client":clientID,"request":connectionRequest,
         "digest":SHA256.hash(data:Data(candidateToken.utf8)).map{String(format:"%02x",$0)}.joined(),"name":ComputerIdentity.displayName]
    }
    private func sendConnectionRequest() {
        guard !connectionRequest.isEmpty,!pending.values.contains(where:{$0.kind=="pair"}) else { return }
        sendOnAvailableLinks(connectionBody(type:"connection.request"));lastRequest=Date()
    }
    private func updateTransport() {
        let previous=transport
        if Date().timeIntervalSince(wifiLastSeen)<ConnectionTiming.linkFresh { transport = .wifi;peer=discovery.devices[device]?.endpoint }
        else if Date().timeIntervalSince(bleLastSeen)<ConnectionTiming.linkFresh { transport = .bluetooth;peer=Self.bleEndpoint }
        if transport != previous && authenticated && !paused && !token.isEmpty {
            if active { voiceLog.info("Voice transport changed to=\(self.transport.rawValue,privacy:.public)") }
            beacon(broadcast:false);lastBeacon=Date()
        }
    }
    private func sendOnAvailableLinks(_ body:[String:Any],type:UInt8=2) {
        if let found=discovery.devices[device],found.hasWiFi { _=send(body,type:type,to:found.endpoint) }
        if bluetooth?.isActive==true { _=send(body,type:type,viaBLE:true) }
    }
    func disconnectDevice() { work.async {
        if self.authenticated { self.send(["type":"pc.disconnect","token":self.token,"device":self.device,"client":self.clientID]) }
        self.cancelConnectionRequest();self.paused=true;self.disconnect("已断开");self.connectionMessage="已断开";self.publish()
        if self.bluetooth?.isActive==true { self.work.asyncAfter(deadline:.now()+0.5) { if self.paused { self.bluetooth?.disconnect() } } }
    } }
    func unpairDevice() { work.async {
        guard self.authenticated else { return };self.request("pc.unpair",extra:[:])
    } }
    @discardableResult private func send(_ body:[String:Any],type:UInt8=2,to target:sockaddr_in?=nil,viaBLE:Bool=false)->Bool {
        var payload=body
        if payload["device"]==nil && type != 3 { payload["device"]=device }
        if payload["client"]==nil && type != 3 { payload["client"]=clientID }
        guard let data=try? JSONSerialization.data(withJSONObject:payload),data.count<=1536 else { return false }
        var packet=Data([type]);packet.append(data)
        if viaBLE || (transport == .bluetooth && target==nil) { return bluetooth?.send(packet)==true }
        guard socketFD>=0 else { return false }
        return LANSocket.send(packet,from:socketFD,to:target ?? peer ?? sockaddr_in())
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
        var body=LANSocket.beacon(port:localPort,token:broadcast ? "" : token,audioReady:AudioSink.deviceUID() != nil,
            hotkeyReady:Shortcut.permitted,epoch:Int(Date().timeIntervalSince1970),tz:TimeZone.current.secondsFromGMT()/60)
        if broadcast {
            guard lanEnabled else { return }
            var sent=false
            for dest in destinations() { if send(body,type:3,to:dest) { sent=true } }
            if !sent && transport == .wifi { state.error="局域网发送失败，请检查网络和局域网权限" }
        } else if !paused { body["device"]=device;body["client"]=clientID;body["primaryTransport"]=transport.rawValue;body["radioPCM"]=1;body["radioFormats"]=RadioFormat.supportedMask;sendOnAvailableLinks(body,type:3) }
    }
    private func sendStatus() {
        if !token.isEmpty { send(["type":"pc.status","token":token,"radioPCM":1,"radioFormats":RadioFormat.supportedMask,"audioReady":AudioSink.deviceUID() != nil,"hotkeyReady":Shortcut.permitted,"epoch":Int(Date().timeIntervalSince1970),"tz":TimeZone.current.secondsFromGMT()/60,"idleHeartbeat":LANSocket.negotiatedHeartbeatSeconds]) }
    }
    func notifyAgent(_ event:AgentEvent) {
        work.async {
            guard self.authenticated,self.peer != nil,!self.token.isEmpty,Date().timeIntervalSince(self.lastSeen)<self.peerTimeout else { DispatchQueue.main.async{self.onAgentResult?("设备离线，本次提醒未发送")};return }
            guard self.agentCapable else { DispatchQueue.main.async{self.onAgentResult?("请先更新支持 Agent 提醒的设备固件")};return }
            guard self.agentPending.count<8 else { return }
            let id=event.noticeID
            self.rememberAgentNotice(event)
            var body=event.deviceBody;body["type"]="agent.notify";body["id"]=id;body["token"]=self.token
            body["ttl_ms"]=10000;body["expires"]=Date().timeIntervalSince1970+10
            self.agentPending[id]=(body,Date().addingTimeInterval(5),Date(),1);self.send(body)
        }
    }
    static func fnv1a64(_ text:String)->UInt64 {
        var hash:UInt64=14695981039346656037
        for byte in text.utf8 { hash=(hash ^ UInt64(byte)) &* 1099511628211 }
        return hash == 0 ? 1 : hash
    }
    private func rememberAgentNotice(_ event:AgentEvent) {
        let hash=String(Self.fnv1a64(event.noticeID))
        if agentNoticeKeys[hash] != nil { agentNoticeOrder.removeAll{$0==hash} }
        agentNoticeKeys[hash]=event.noticeKey
        agentNoticeOrder.append(hash)
        if agentNoticeOrder.count>128 { agentNoticeKeys.removeValue(forKey:agentNoticeOrder.removeFirst()) }
    }
    private func tick() {
        let now=Date()
        // Fresh connectable advertisements drive automatic BLE attachment via
        // onFound. Do not repeatedly connect to a radio that stopped advertising.
        if radioPlayback != nil && now.timeIntervalSince(radioProgressAt)>12 { stopRadioPlayback() }
        updateTransport()
        if !connectionRequest.isEmpty {
            if now.timeIntervalSince(requestStarted)>55 { cancelConnectionRequest();paused=true;connectionMessage="连接确认已超时，请重试" }
            else if now.timeIntervalSince(lastRequest)>1.5 { sendConnectionRequest() }
        }
        if authenticated && now.timeIntervalSince(lastSeen)>peerTimeout { disconnect("设备已断开，未保存的修改已保留");connectionMessage="正在重新连接…" }
        if now.timeIntervalSince(lastDiscovery)>(authenticated ? 10 : 2) {
            beacon(broadcast:true);lastDiscovery=now
        }
        if active && (now.timeIntervalSince(lastAudio)>ConnectionTiming.voiceTimeout || now.timeIntervalSince(lastKey)>180 || !Shortcut.permitted) { endVoice(immediate:true) }
        let readiness="\(AudioSink.deviceUID() != nil)/\(Shortcut.permitted)/\(TimeZone.current.secondsFromGMT())"
        let changed=readiness != lastReadiness;lastReadiness=readiness
        if now.timeIntervalSince(lastBeacon)>=1 || (changed && peer != nil) {
            if !paused && peer != nil && !token.isEmpty { beacon(broadcast:false) };lastBeacon=now
        }
        for (id,entry) in pending where now.timeIntervalSince(entry.at)>(entry.kind=="pair" && transport == .bluetooth ? 5 : 1.2) {
            if entry.tries>=(entry.kind=="pair" && transport == .bluetooth ? 18 : 4) { pending.removeValue(forKey:id);if entry.kind=="pair" { cancelConnectionRequest();paused=true;connectionMessage="连接未完成，请重试" };if entry.kind.hasPrefix("power.") { state.powerLoading=false;state.powerError="读取超时，请重试；下方保留上次读取结果。" } else if entry.kind.hasPrefix("radio.") { cancelRadio("电台同步超时，原列表已保留，请重新读取") } else if entry.kind=="weather.cache" { nextWeather=now.addingTimeInterval(60) } else { DispatchQueue.main.async { self.onResult?(false,"设备没有响应，请检查连接后重试") } } }
            else {
                // Small Wi-Fi heartbeats can remain fresh while a larger
                // diagnostic response is lost. Retry this read over both
                // authenticated paths, retaining the same correlation ID.
                if entry.kind=="power.get" { sendOnAvailableLinks(entry.body) }
                else { send(entry.body) }
                pending[id]=(entry.body,entry.tries+1,now,entry.kind)
            }
        }
        for (id,entry) in agentPending where now.timeIntervalSince(entry.at)>0.8 {
            if now>entry.expires || entry.tries>=4 { agentPending.removeValue(forKey:id);DispatchQueue.main.async{self.onAgentResult?("设备未确认提醒，请检查连接")} }
            else { send(entry.body);agentPending[id]=(entry.body,entry.expires,now,entry.tries+1) }
        }
        refreshWeather();publish()
    }
    private func receive() {
        var bytes=[UInt8](repeating:0,count:2048)
        for _ in 0..<32 {
            var from=sockaddr_in();var size=socklen_t(MemoryLayout<sockaddr_in>.size)
            let n=withUnsafeMutablePointer(to:&from) { p in p.withMemoryRebound(to:sockaddr.self,capacity:1) { Darwin.recvfrom(socketFD,&bytes,bytes.count,0,$0,&size) } }
            if n<=0 { break }
            let data=Data(bytes.prefix(n))
            guard from.sin_family==sa_family_t(AF_INET),from.sin_port==LANSocket.discoveryPort.bigEndian else { continue }
            processPacket(data,from:from,viaBLE:false)
        }
        publish()
    }
    private func processPacket(_ data:Data,from:sockaddr_in,viaBLE:Bool) {
        guard let packet=Packet.parse(data) else { return }
        if case .event(let body)=packet,body["event"] as? String=="connection.reply" {
            guard !connectionRequest.isEmpty,body["request"] as? String==connectionRequest,!paused else { return }
            if !viaBLE { guard let found=discovery.devices[device],LANSocket.sameEndpoint(found.endpoint,from) else { return } }
            switch body["state"] as? String {
            case "pending":
                if !requestDelivered { requestStarted=Date();requestDelivered=true }
                connectionMessage="请在设备上按上键确认"
            case "accepted":
                peer=from;transport=viaBLE ? .bluetooth : .wifi
                if !pending.values.contains(where:{$0.kind=="pair"}) {
                    connectionMessage="正在完成连接…";request("pair",extra:["request":connectionRequest])
                }
            case "denied","expired","cancelled","busy":
                let status=body["state"] as? String ?? ""
                let message=status=="denied" ? "设备已拒绝连接" : status=="busy" ? "设备忙，请稍后重试" : "连接确认已取消或超时"
                cancelConnectionRequest();paused=true;connectionMessage=message;bluetooth?.disconnect()
            default:break
            }
            return
        }
        if case .event(let body)=packet,let found=(viaBLE ? discovery.acceptBLE(body,id:device) : discovery.accept(body,from:from)) {
            guard !paused,found.id==device else { return }
            if !token.isEmpty,body["authChecked"] as? Bool==true,body["authorized"] as? Bool==false {
                if connectIntent { beginConnectionRequest() }
                else {
                    let message=found.paired ? "设备已切换到其他电脑" : "设备已解除配对"
                    disconnect(message);paused=true;connectionMessage=message;bluetooth?.disconnect()
                    if !found.paired { PairingStore.remove(device);token="" }
                }
                return
            }
            if token.isEmpty { if peer==nil || !viaBLE { peer=from };return }
            guard body["authorized"] as? Bool==true else {
                if peer==nil { peer=found.hasWiFi ? found.endpoint : Self.bleEndpoint }
                if Date().timeIntervalSince(lastBeacon)>1 { beacon(broadcast:false);lastBeacon=Date() }
                return
            }
            let rebooted=bootID != nil && (body["boot"] as? Int) != bootID
            if rebooted { disconnect("设备重新连接，请确认未完成的保存") }
            let reconnect = !authenticated
            weatherCapable=body["weatherCache"] as? Int==1;agentCapable=body["agentNotify"] as? Int==1;slowHeartbeat=body["idleHeartbeat"] as? Int==30
            state.powerAvailable=body["powerDiagnostics"] as? Int==1
            if reconnect { recentEvents=[];settingsRevision=nil }
            bootID=body["boot"] as? Int
            authenticated=true;connectIntent=false;lastSeen=Date();connectionMessage=""
            if viaBLE { bleLastSeen=Date() } else { wifiLastSeen=Date() }
            updateTransport();state.address=transport.title
            let revision=(body["settingsRevision"] as? NSNumber)?.uint32Value
            if (reconnect || (revision != nil && revision != settingsRevision)) && pending.isEmpty { request("config.get",extra:[:]) }
            return
        }
        guard !paused else { return }
        if !viaBLE { guard let found=discovery.devices[device],LANSocket.sameEndpoint(found.endpoint,from) else { return } }
        if authenticated {
            lastSeen=Date();if viaBLE { bleLastSeen=Date() } else { wifiLastSeen=Date() };updateTransport()
        }
        switch packet {
        case .ping:break
        case .audio(let seq,let pcm):
            guard authenticated,active else { return }
            if let previous=sequence {
                let delta=seq &- previous
                if delta==0 || delta>128 { return }
                if delta>1 { state.dropped+=Int(delta)-1;for _ in 0..<min(3,Int(delta)-1) { sink.append([Int16](repeating:0,count:1600)) } }
            }
            let received=Date()
            if state.packets==voiceInitialPackets { voiceLog.info("Voice first audio ms=\(Int(received.timeIntervalSince(self.voiceStarted)*1000)) transport=\(self.transport.rawValue,privacy:.public)") }
            else { voiceMaxGap=max(voiceMaxGap,received.timeIntervalSince(lastAudio)) }
            sequence=seq;state.packets+=1;lastAudio=received;sink.append(pcm)
        case .event(let body):
            guard authenticated || (pairing && body["event"] as? String=="config.reply") else { return };handle(body)
        }
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
        case "agent.dismissed":
            guard let id=body["id"] as? String,let key=agentNoticeKeys[id] else { return }
            DispatchQueue.main.async { self.onAgentDismiss?(key) }
        case "pc.ping":
            if authenticated,!token.isEmpty { beacon(broadcast:false) }
        case "voice.ignored":
            voiceLog.info("Voice ignored reason=\(body["reason"] as? String ?? "unknown",privacy:.public)")
        case "weather.reply":
            guard let id=body["id"] as? String,let entry=pending[id],entry.kind=="weather.cache" else{return}
            pending.removeValue(forKey:id)
            nextWeather=Date().addingTimeInterval(body["ok"] as? Bool==true && weatherHasAir ? 900:60)
        case "radio.open":
            stopWeather()
            guard !active,let session=body["session"] as? String,let urlText=body["url"] as? String,
                  let url=URL(string:urlText),urlText.utf8.count<=256,let format=RadioFormat(rawValue:body["format"] as? Int ?? 1),
                  let delivery=RadioDelivery(session:session,window:body["window"] as? Int ?? 4,format:format) else { return }
            if radioPlayback?.session != session {
                stopRadioPlayback();radioFailureSent=false;radioDelivery=delivery
                // Radio decodes without a local audio output, so macOS cannot
                // infer that background timers carry live audio. Scope this
                // precision request to the session; never retain it while idle.
                radioActivity=ProcessInfo.processInfo.beginActivity(options:[.userInitiatedAllowingIdleSystemSleep,.latencyCritical],reason:"Streaming audio to FoloToy")
                radioProgressAt=Date();radioTickAt=Date();radioMaxTickGap=0
                let playback=RadioPlayback(session:session,url:url,format:format) { [weak self] in self?.work.async { [weak self] in self?.flushRadioPlayback() } };radioPlayback=playback
                let clock=DispatchSource.makeTimerSource(queue:work)
                clock.schedule(deadline:.now(),repeating:.milliseconds(20),leeway:.milliseconds(3))
                clock.setEventHandler{ [weak self] in
                    guard let self=self else{return};let now=Date()
                    self.radioMaxTickGap=max(self.radioMaxTickGap,now.timeIntervalSince(self.radioTickAt));self.radioTickAt=now
                    self.flushRadioPlayback()
                };radioClock=clock;clock.resume()
            }
        case "radio.progress":
            if let delivery=radioDelivery,body["session"] as? String==delivery.session {radioProgressAt=Date()}
            radioDelivery?.accept(body)
            if let delivery=radioDelivery {bluetooth?.acknowledgeRadio(session:delivery.nonce,next:delivery.acknowledged,mask:delivery.mask)}
            flushRadioPlayback()
        case "radio.close":
            if body["session"] as? String==radioPlayback?.session { stopRadioPlayback() }
        case "voice.start":
            stopWeather();stopRadioPlayback()
            if active { endVoice(immediate:true) }
            guard authenticated,!token.isEmpty else {
                voiceLog.info("Voice start ignored: unauthenticated or missing token")
                return
            }
            guard !active else { return }
            do {
                try sink.start();try keyboard.start(shortcut);active=true;ending=false;generation+=1;sequence=nil;lastAudio=Date();lastKey=Date();state.error=""
                voiceStarted=Date();voiceInitialPackets=state.packets;voiceInitialDropped=state.dropped;voiceMaxGap=0
                voiceLog.info("Voice started transport=\(self.transport.rawValue,privacy:.public)")
            }
            catch { sink.stop();keyboard.stop();state.error=error.localizedDescription }
        case "voice.end": endVoice(immediate:false)
        case "key.action":
            voiceLog.info("Key action received authenticated=\(self.authenticated) active=\(self.active) permitted=\(Shortcut.permitted)")
            if authenticated,!token.isEmpty,!active,body["action"] as? String=="enter" { keyboard.enter() }
        case "radio.verified":
            if let url=body["url"] as? String { DispatchQueue.main.async { self.onRadioVerified?(url) } }
        case "radio.reply":
            guard let id=body["id"] as? String,let entry=pending[id],entry.kind.hasPrefix("radio."),var transfer=radioTransfer else { return }
            pending.removeValue(forKey:id)
            if body["verified"] as? Bool==true,let station=body["station"] as? [String:Any],let url=station["url"] as? String { DispatchQueue.main.async { self.onRadioVerified?(url) } }
            do {
                if try transfer.accept(body) {
                    radioTransfer=nil
                    let source=device
                    DispatchQueue.main.async { self.onRadio?(transfer.stations,transfer.revision,transfer.capacity,source);self.onRadioLoading?(false);if transfer.writing { self.onResult?(true,"电台及顺序已保存到设备") } }
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
                token=candidateToken;candidateToken="";connectionRequest="";requestDelivered=false;pairing=false;authenticated=true;connectIntent=false;lastSeen=Date();connectionMessage=""
                if transport == .wifi { wifiLastSeen=Date() } else { bleLastSeen=Date() }
                if PairingStore.save(token,device:device) { UserDefaults.standard.set(device,forKey:"device") }
                else { state.error="无法保存本地配对信息，重启后需要重新配对。请检查账户文件权限。" }
                beacon(broadcast:false)
            }
            if entry.kind=="pair" { pairing=false;if !ok { connectionMessage=body["error"] as? String ?? "配对失败，请重试" } }
            if ok,entry.kind=="config.get" { authenticated=true;lastSeen=Date();connectionMessage="";UserDefaults.standard.set(device,forKey:"device") }
            if ok,entry.kind=="pc.unpair" {
                if transport == .bluetooth { work.asyncAfter(deadline:.now()+0.3) { self.bluetooth?.disconnect() } }
                PairingStore.remove(device);UserDefaults.standard.removeObject(forKey:"device")
                token="";candidateToken="";paused=true;disconnect("已解除配对")
                DispatchQueue.main.async { self.onResult?(true,"已解除配对") };return
            }
            if ok { receiveSettings(body) }
            if ok,(entry.kind=="config.get" || entry.kind=="pair"),radioTransfer==nil,(body["settings"] as? [String:Any])?["radio_list_version"] as? Int==1 { beginRadioRead() }
            if entry.kind != "config.get" { DispatchQueue.main.async { self.onResult?(ok,ok ? "已保存到设备" : (body["error"] as? String ?? "设备拒绝了这次操作")) } }
        case "config.changed":
            if !token.isEmpty { receiveSettings(body) }
        default: break
        }
    }
    private func stopWeather() {
        weatherGeneration+=1;weatherJob?.cancel();weatherJob=nil
        pending=pending.filter{$0.value.kind != "weather.cache"}
    }
    private func refreshWeather() {
        guard authenticated,!paused,weatherCapable,!active,radioPlayback==nil,weatherJob==nil,pending.isEmpty,Date()>=nextWeather,let location=weatherLocation else{return}
        nextWeather=Date().addingTimeInterval(60);weatherGeneration+=1;let expected=weatherGeneration
        weatherJob=WeatherFetch(location:location) { [weak self] result in self?.work.async { [weak self] in
            guard let self=self,self.weatherGeneration==expected,self.authenticated,self.weatherLocation==location else{return}
            self.weatherJob=nil
            guard !self.active,self.radioPlayback==nil else{return}
            if case .success(let sample)=result {
                self.weatherHasAir=sample["air"] != nil
                self.request("weather.cache",extra:["latitude":location.latitude,"longitude":location.longitude,"city":location.city,"sample":sample])
            }
        } }
    }
    private func stopRadioPlayback() {
        radioClock?.cancel();radioClock=nil;
        if let activity=radioActivity {ProcessInfo.processInfo.endActivity(activity);radioActivity=nil}
        bluetooth?.discardRadio();radioPlayback?.stop();radioPlayback=nil;radioDelivery=nil
    }
    private func flushRadioPlayback() {
        guard authenticated,!paused,let playback=radioPlayback,var delivery=radioDelivery else { return }
        while delivery.hasCredit,let block=playback.take() { _=delivery.append(block) }
        let found=discovery.devices[device],wifi=found.map{$0.hasWiFi && socketFD>=0} ?? false
        for (packet,ble) in delivery.packets(now:Date().timeIntervalSince1970,wifi:wifi,ble:bluetooth?.isActive==true,primaryBLE:transport == .bluetooth) {
            if ble { _=bluetooth?.send(packet) }
            else if let endpoint=found?.endpoint { _=LANSocket.send(packet,from:socketFD,to:endpoint) }
        }
        radioDelivery=delivery
        if Date().timeIntervalSince(radioReportAt)>=5 {
            radioReportAt=Date()
            voiceLog.info("Radio delivery next=\(delivery.next) played=\(delivery.acknowledged) mask=\(delivery.mask) queued=\(playback.queued) retries=\(delivery.retransmissions) ble=\(delivery.preferBLE) nr=\(self.bluetooth?.radioWithoutResponse ?? false) tx=\(self.bluetooth?.queuedWrites ?? 0) timerGapMs=\(Int(self.radioMaxTickGap*1000))")
            radioMaxTickGap=0
        }
        if let error=playback.failure(),delivery.frames.isEmpty,!radioFailureSent {
            radioFailureSent=true
            sendOnAvailableLinks(["type":"playback.status","token":token,"session":playback.session,"failed":true])
            voiceLog.error("Radio source stopped: \(error,privacy:.private)")
        }
    }
    private func receiveSettings(_ body:[String:Any]) {
        guard let settings=body["settings"],let data=try? JSONSerialization.data(withJSONObject:settings),var prefs=try? JSONDecoder().decode(DeviceSettings.self,from:data),prefs.valid else { return }
        prefs.requireWeather()
        let location=WeatherLocation(prefs)
        if weatherLocation != location {stopWeather();weatherLocation=location;nextWeather = .distantPast}
        settingsRevision=(body["settingsRevision"] as? NSNumber)?.uint32Value
        let value=prefs,source=device;DispatchQueue.main.async { self.onSettings?(value,source) }
    }
    private func endVoice(immediate:Bool) {
        guard active else { return }
        ending=true
        generation+=1;let expected=generation
        let stop={ [self] in
            guard generation==expected else { return }
            keyboard.stop();sink.stop();active=false;ending=false;sequence=nil
            voiceLog.info("Voice stopped packets=\(self.state.packets-self.voiceInitialPackets) dropped=\(self.state.dropped-self.voiceInitialDropped) max_gap_ms=\(Int(self.voiceMaxGap*1000)) immediate=\(immediate)")
            publish()
        }
        if immediate { stop() } else { work.asyncAfter(deadline:.now()+0.18,execute:stop) }
    }
    private func request(_ type:String,extra:[String:Any]) {
        let id=UUID().uuidString
        var body=extra;body["type"]=type;body["id"]=id;body["device"]=device;body["client"]=clientID;body["token"]=type=="pair" ? candidateToken : token
        pending[id]=(body,1,Date(),type);send(body)
    }
    func save(_ prefs:DeviceSettings,changes:[String:Any]) { work.async {
        guard self.authenticated,prefs.validForSave,!self.token.isEmpty else {
            DispatchQueue.main.async { self.onResult?(false,"设备未连接，请重新连接后保存") };return
        }
        self.request("config.set",extra:["settings":changes])
    } }
    func reload() { work.async { if self.authenticated,!self.token.isEmpty { self.request("config.get",extra:[:]) } } }
    func readPower(reset:Bool=false) {
        work.async {
            guard self.authenticated,self.peer != nil,!self.token.isEmpty,self.state.powerAvailable,!self.state.powerLoading else { return }
            self.state.powerLoading=true;self.state.powerError="";self.request(reset ? "power.reset" : "power.get",extra:[:]);self.publish()
        }
    }
    private func sendRadioStep() {
        guard let transfer=radioTransfer else { return }
        let (type,extra)=transfer.command;request(type,extra:extra)
    }
    private func beginRadioRead() {
        guard authenticated,!token.isEmpty,peer != nil,radioTransfer==nil else { return }
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
            guard self.authenticated,self.peer != nil,!self.token.isEmpty,!self.active,self.radioTransfer==nil else { DispatchQueue.main.async { self.onResult?(false,"请等待设备连接及电台读取完成，并结束语音输入") };return }
            do { self.radioTransfer=try RadioListTransfer(stations:stations,revision:revision,capacity:capacity);DispatchQueue.main.async { self.onRadioLoading?(true) };self.sendRadioStep() }
            catch { DispatchQueue.main.async { self.onResult?(false,error.localizedDescription) } }
        }
    }
    func setShortcut(_ spec:String) throws {
        _=try Shortcut.resolve(spec);UserDefaults.standard.set(spec,forKey:"shortcut")
        work.async { self.endVoice(immediate:true);self.shortcut=spec }
    }
    private func disconnect(_ message:String) {
        stopWeather();weatherLocation=nil;stopRadioPlayback()
        authenticated=false;pairing=false;connectionMessage="";slowHeartbeat=false;wifiLastSeen = .distantPast;bleLastSeen = .distantPast;state.powerAvailable=false;state.powerLoading=false
        agentPending.removeAll();agentCapable=false;
        cancelRadio(message)
        let interrupted = !pending.isEmpty
        pending.removeAll();peer=nil;state.address="";lastSeen = .distantPast;lastBeacon = .distantPast
        settingsRevision=nil;bootID=nil;recentEvents.removeAll();endVoice(immediate:true)
        if interrupted { DispatchQueue.main.async { self.onResult?(false,message) } }
    }
    static func testConnections(_ check:(Bool,String)->Void) {
        let engine=Engine(preview:true)
        engine.clientID="11111111-1111-1111-1111-111111111111";engine.device="aabbccddeeff"
        engine.setBluetoothPairingRepair("112233445566",required:true)
        check(!engine.state.bluetoothPairingRepairRequired,"late BLE repair state from another device is ignored")
        engine.setBluetoothPairingRepair(engine.device,required:true)
        check(engine.state.bluetoothPairingRepairRequired,"lost pairing records expose a repair action for the selected device")
        engine.setBluetoothPairingRepair(engine.device,required:false)
        var lan=sockaddr_in();lan.sin_family=sa_family_t(AF_INET);lan.sin_port=LANSocket.discoveryPort.bigEndian;lan.sin_addr.s_addr=inet_addr("192.168.1.20")
        let hello:[String:Any]=["event":"device.hello","device":engine.device,"paired":true,"authorized":true,"authChecked":true,"boot":1,"agentNotify":1]
        _=engine.discovery.accept(hello,from:lan)
        engine.discovery.foundBLE(engine.device,identifier:UUID(),connectable:true)
        func feed(_ body:[String:Any],ble:Bool=false) {
            var data=Data([2]);data.append(try! JSONSerialization.data(withJSONObject:body));engine.processPacket(data,from:ble ? Self.bleEndpoint : lan,viaBLE:ble)
        }
        feed(["event":"voice.start"])
        check(!engine.active && !engine.authenticated,"unauthenticated voice start is ignored")
        engine.beginConnectionRequest()
        let request=engine.connectionRequest,body=engine.connectionBody(type:"connection.request")
        check(!engine.requestDelivered,"request transmission alone cannot claim the device is awaiting confirmation")
        feed(["event":"connection.reply","request":UUID().uuidString.lowercased(),"state":"pending"])
        check(!engine.requestDelivered,"ignore delivery acknowledgement for another request")
        feed(["event":"connection.reply","request":request,"state":"pending"])
        let deliveredAt=engine.requestStarted
        check(engine.requestDelivered && engine.connectionMessage=="请在设备上按上键确认","matching device acknowledgement enables the physical-confirmation prompt")
        feed(["event":"connection.reply","request":request,"state":"pending"])
        check(engine.requestStarted==deliveredAt,"repeated pending replies cannot extend the approval window")
        check(body["token"]==nil && (body["digest"] as? String)?.count==64,"approval request reveals a commitment, not the credential")
        feed(["event":"connection.reply","request":UUID().uuidString,"state":"accepted"])
        check(engine.pending.isEmpty,"ignore approval for another request")
        feed(["event":"connection.reply","request":request,"state":"accepted"])
        check(engine.pending.count==1 && engine.pending.values.first?.body["request"] as? String==request,"physical acceptance commits the same request")
        feed(["event":"connection.reply","request":request,"state":"accepted"],ble:true)
        check(engine.pending.count==1,"dual-link acceptance cannot create duplicate commits")
        engine.pending=[:];engine.connectionRequest="";engine.pairing=false;engine.token=String(repeating:"a",count:64)
        engine.peer=nil
        var rediscovery=hello;rediscovery["authChecked"]=false;rediscovery["authorized"]=false;feed(rediscovery)
        check(engine.peer != nil && !engine.paused,"rediscovery restores an endpoint before authenticated reconnect")
        feed(hello,ble:true);feed(hello)
        check(engine.authenticated && engine.transport == .wifi && Date().timeIntervalSince(engine.bleLastSeen)<1,"one authorized session keeps Wi-Fi and BLE attached")
        feed(["event":"pc.ping"])
        feed(["event":"voice.ignored","reason":"not_ready"])
        check(engine.authenticated && engine.peer != nil && engine.transport == .wifi && !engine.active,
              "readiness ping and ignored voice leave connection state intact")
        let notice=AgentEvent(agent:.codex,kind:.completed,session:"dismiss-test",key:AgentEvent.digest("dismiss-test"),project:"")
        engine.rememberAgentNotice(notice)
        var dismissedKey:String?
        engine.onAgentDismiss={ dismissedKey=$0 }
        feed(["event":"agent.dismissed","id":String(Self.fnv1a64(notice.noticeID))])
        RunLoop.current.run(until:Date().addingTimeInterval(0.01))
        check(dismissedKey==notice.noticeKey,"device dismissal resolves the original notice key")
        dismissedKey=nil
        feed(["event":"agent.dismissed","id":"0"])
        RunLoop.current.run(until:Date().addingTimeInterval(0.01))
        check(dismissedKey==nil,"unknown dismissal hashes are ignored")
        engine.wifiLastSeen=Date().addingTimeInterval(-7);engine.updateTransport()
        check(engine.transport == .bluetooth && engine.authenticated,"LAN timeout falls back to BLE without losing authorization")
        do {
            let receiver=try LANSocket.open(),sender=try LANSocket.open()
            defer { Darwin.close(receiver.descriptor);Darwin.close(sender.descriptor) }
            var endpoint=Self.bleEndpoint;endpoint.sin_port=receiver.port.bigEndian
            let fallback=Engine(preview:true);fallback.device=engine.device;fallback.clientID=engine.clientID
            fallback.token=engine.token;fallback.authenticated=true;fallback.peer=Self.bleEndpoint
            fallback.transport = .bluetooth;fallback.bleLastSeen=Date();fallback.lastSeen=Date()
            fallback.socketFD=sender.descriptor;fallback.lastDiscovery=Date();fallback.lastBeacon=Date()
            _=fallback.discovery.accept(hello,from:endpoint)
            // No BLE transport exists here: the first attempt really cannot
            // arrive. Age it and exercise the production timeout path.
            fallback.request("power.get",extra:[:])
            let id=fallback.pending.keys.first!,entry=fallback.pending[id]!
            fallback.pending[id]=(entry.body,1,Date().addingTimeInterval(-2),entry.kind)
            fallback.tick()
            var received=false,bytes=[UInt8](repeating:0,count:2048)
            while true {
                let count=Darwin.recv(receiver.descriptor,&bytes,bytes.count,0)
                if count<=0 { break }
                if let body=(try? JSONSerialization.jsonObject(with:Data(bytes[1..<count]))) as? [String:Any],body["type"] as? String=="power.get" {
                    received=body["id"] as? String==id
                }
            }
            check(received && fallback.pending[id]?.tries==2,"timed-out power read reaches alternate real UDP endpoint with unchanged request ID")
            fallback.pending=[:];fallback.socketFD = -1
        } catch { check(false,"power read alternate-path integration: \(error.localizedDescription)") }
        engine.connectIntent=false
        var denied=hello;denied["authorized"]=false;feed(denied)
        check(engine.paused && !engine.authenticated,"revoked computer stops automatic reconnect")
        engine.paused=false;engine.beginConnectionRequest()
        feed(["event":"connection.reply","request":engine.connectionRequest,"state":"denied"])
        check(engine.paused && engine.connectionRequest.isEmpty,"device refusal ends the connection attempt")

        let switching=Engine(preview:true)
        switching.clientID=engine.clientID
        let first="001122334455",second="001122334466"
        var otherLAN=lan;otherLAN.sin_addr.s_addr=inet_addr("192.168.1.21")
        var firstHello=hello;firstHello["device"]=first
        var secondHello=hello;secondHello["device"]=second
        _=switching.discovery.accept(firstHello,from:lan)
        _=switching.discovery.accept(secondHello,from:otherLAN)
        func packet(_ body:[String:Any],from:sockaddr_in) {
            var bytes=Data([2]);bytes.append(try! JSONSerialization.data(withJSONObject:body))
            switching.processPacket(bytes,from:from,viaBLE:false)
        }
        switching.device=first;switching.token=String(repeating:"a",count:64)
        switching.peer=lan;packet(firstHello,from:lan)
        check(switching.authenticated && !switching.pending.isEmpty,"first device has an authenticated pending read")
        switching.selectDeviceNow(second)
        check(!switching.state.bluetoothPairingRepairRequired,"device selection clears unrelated BLE repair state")
        let secondRequest=switching.connectionRequest
        check(switching.device==second && !switching.authenticated && switching.pending.isEmpty && switching.recentEvents.isEmpty,"device switch clears old authentication, requests and event deduplication")
        check(LANSocket.sameEndpoint(switching.peer!,otherLAN) && !secondRequest.isEmpty,"second device gets its own endpoint and confirmation request")
        packet(firstHello,from:lan)
        packet(["event":"config.reply","id":UUID().uuidString,"ok":true],from:lan)
        check(switching.device==second && !switching.authenticated && switching.pending.isEmpty && switching.connectionRequest==secondRequest,"late old-device discovery and business packets cannot revive its session")
        packet(["event":"connection.reply","request":UUID().uuidString,"state":"accepted"],from:otherLAN)
        check(switching.pending.isEmpty,"stale approval after switching is ignored")
        switching.connectionRequest="";switching.pairing=false;switching.token=String(repeating:"b",count:64)
        packet(secondHello,from:otherLAN)
        check(switching.authenticated && switching.transport == .wifi,"new device can authenticate independently")
        var previous=""
        for index in 0..<100 {
            let target=index%2==0 ? first:second
            switching.selectDeviceNow(target)
            check(switching.device==target && !switching.authenticated && switching.pending.isEmpty && !switching.active,"rapid switches cannot carry an old voice or pending write")
            check(!switching.connectionRequest.isEmpty && switching.connectionRequest != previous,"each switch has a fresh confirmation identity")
            previous=switching.connectionRequest
        }
        switching.cancelConnectionRequest();switching.paused=true;switching.disconnect("cancelled")
        packet(secondHello,from:otherLAN)
        check(switching.paused && !switching.authenticated && switching.connectionRequest.isEmpty,"cancelled switching cannot reconnect from late discovery")
        switching.connectionMessage="已断开";switching.retryNow(resume:false)
        check(switching.paused && !switching.authenticated && switching.connectionRequest.isEmpty && switching.connectionMessage=="已断开","foreground permission refresh preserves an explicit disconnect")

        let radio=Engine(preview:true)
        radio.work.sync {
            let session="0123456789abcdef"
            radio.handle(["event":"radio.open","session":session,"url":"http://127.0.0.1:9/absent","window":10])
            check(radio.radioPlayback != nil && radio.radioActivity != nil,"live radio owns its background timing activity")
            radio.handle(["event":"radio.close","session":"1111111111111111"])
            check(radio.radioPlayback != nil,"a stale close cannot stop current radio")
            radio.radioProgressAt=Date().addingTimeInterval(-13)
            radio.handle(["event":"radio.progress","session":session,"next":0,"mask":0]);radio.tick()
            check(radio.radioPlayback != nil,"current session progress renews the radio lease")
            radio.radioProgressAt=Date().addingTimeInterval(-13);radio.tick()
            check(radio.radioPlayback==nil && radio.radioActivity==nil && radio.radioDelivery==nil,"lost close or stopped progress cancels decoder, cache, timer and background activity")
            radio.stopRadioPlayback()
            radio.handle(["event":"radio.open","session":session,"url":"http://127.0.0.1:9/absent","window":16,"format":2])
            check(radio.radioDelivery?.format == .buffered16k && radio.radioDelivery?.window==16,"explicit format 2 negotiates the larger receiver cache")
            radio.stopRadioPlayback()
            radio.handle(["event":"radio.open","session":session,"url":"http://127.0.0.1:9/absent","window":16,"format":3])
            check(radio.radioPlayback==nil && radio.radioActivity==nil,"unsupported format cannot create a decoder or timing activity")
        }
    }
    func stop(completion:(()->Void)?=nil) {
        work.sync {
            if authenticated { send(["type":"pc.disconnect","token":token,"device":device,"client":clientID]) }
            cancelConnectionRequest()
            disconnect("连接已结束，未确认的修改请重新读取");
            keyboard.stop();sink.stop();timer?.cancel();timer=nil;socketGeneration+=1
            if let reader=reader {
                let fd=socketFD
                reader.setCancelHandler { Darwin.close(fd);if let completion=completion { DispatchQueue.main.async(execute:completion) } }
                reader.cancel()
            } else if let completion=completion { DispatchQueue.main.async(execute:completion) }
            bluetooth?.disconnect();reader=nil;socketFD = -1;localPort=0
        }
    }
}
