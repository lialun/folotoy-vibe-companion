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
    private var transportPolicy=TransportPolicy()
    private var voiceLoss=VoiceLossWindow()
    private var voiceWiFiPackets=0,voiceBLEPackets=0
    private var voiceMerge:VoiceMerge?,voiceClock:DispatchSourceTimer?
    private var voiceEvents=VoiceSessionEvents()
    private var voiceEventScope=""
    private var voiceTailDeadline:TimeInterval?
    private var authenticated=false,pairing=false,running=false,tearingDown=false
    private let lifecycle=EngineLifecycle()
    private var paused:Bool {get{lifecycle.userPaused} set{lifecycle.userPaused=newValue}}
    private var powerSuspended:Bool {lifecycle.isSuspended}
    var connectionPaused:Bool {lifecycle.userPaused}
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
    private var quotaCapable=false,quotaSamples:[AgentID:AgentQuotaSample]=[:],quotaSent:[AgentID:(String,Date)]=[:]
    private var quotaAcceptanceReply:(([String:Any])->Void)?
    private var slowHeartbeat=false,lastReadiness=""
    private var peerTimeout:TimeInterval { ConnectionTiming.sessionTimeout }
    private var agentPending:[String:(body:[String:Any],expires:Date,at:Date,tries:Int)]=[:]
    var onAgentResult:((String)->Void)?
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
    init(preview:Bool=false,lanEnabled:Bool=true,connectionPaused:Bool=false) {
        self.lanEnabled=lanEnabled
        lifecycle.userPaused=connectionPaused
        if preview { return }
        clientID=ComputerIdentity.load()
        device=UserDefaults.standard.string(forKey:"device") ?? ""
        transport=ConnectionTransport(rawValue:UserDefaults.standard.string(forKey:"connectionTransport") ?? "wifi") ?? .wifi
        token=PairingStore.load(device) ?? ""
        shortcut=UserDefaults.standard.string(forKey:"shortcut") ?? "ctrl+option+v"
    }
    func start() { work.async {self.startNow()} }
    private func startNow() {
        guard !running,!powerSuspended else { return }
        running=true
        keyboard.resume(if:{!self.powerSuspended})
        let ble=BluetoothLink(queue:self.work);self.bluetooth=ble
        ble.onFound={ [weak self,weak ble] id,identifier,connectable in
            guard let self=self,!self.powerSuspended else { return }
            self.discovery.foundBLE(id,identifier:identifier,connectable:connectable)
            if id==self.device,!self.paused,(!self.token.isEmpty || !self.connectionRequest.isEmpty) {
                if connectable { ble?.connect(id) }

            }
            self.publish()
        }
        ble.onPacket={ [weak self] id,data in
            guard let self=self,!self.powerSuspended,id==self.device else { return }
            self.processPacket(data,from:Self.bleEndpoint,viaBLE:true);self.publish()
        }
        ble.onReady={ [weak self] in
            guard let self=self,!self.powerSuspended,!self.paused else { return }
            if self.peer==nil { self.peer=Self.bleEndpoint }
            if !self.token.isEmpty { self.beacon(broadcast:false) }
            self.sendConnectionRequest();self.lastBeacon=Date();self.publish()
        }
        ble.onDisconnected={ [weak self] message in
            guard let self=self,!self.powerSuspended else { return }
            self.bleLastSeen = .distantPast;self.updateTransport()
            if !self.paused && Date().timeIntervalSince(self.wifiLastSeen)>ConnectionTiming.linkFresh { self.connectionMessage=message }
            self.publish()
        }
        ble.onAuthenticationFailed={ [weak self] message in
            guard let self=self,!self.powerSuspended else { return }
            self.bleLastSeen = .distantPast
            if Date().timeIntervalSince(self.wifiLastSeen)>ConnectionTiming.linkFresh { self.connectionMessage=message }
            self.publish()
        }
        ble.onStatus={ [weak self] status in
            guard let self=self,!self.powerSuspended else { return };self.state.bluetoothStatus=status
            if !self.paused && !self.authenticated && self.connectionRequest.isEmpty { self.connectionMessage=status }
            self.publish()
        }
        ble.onPairingRepairChanged={ [weak self] id,required in
            guard let self=self,!self.powerSuspended else {return};self.setBluetoothPairingRepair(id,required:required)
        }
        self.startTimer();self.open()
    }
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
        state.paired = !token.isEmpty && authenticated;state.recording=active
        if !powerSuspended {state.audio=AudioSink.deviceUID() != nil}
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
        guard running,!powerSuspended else { return }
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
        guard self.running,!self.powerSuspended,!self.device.isEmpty else { return }
        self.paused=false
        if !self.authenticated { self.connectIntent=true;self.connectionMessage="正在连接蓝牙…" }
        if !self.authenticated && self.connectionRequest.isEmpty && self.token.isEmpty { self.beginConnectionRequest() }
        self.bluetooth?.scan();self.bluetooth?.connect(self.device,resetBackoff:true);self.publish()
    } }
    private func selectDeviceNow(_ id:String) {
        guard running,!powerSuspended,let found=discovery.devices[id] else { return }
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
    func cancelConnection() { paused=true;work.async {
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
        let severe=active && !ending && voiceMerge==nil && voiceLoss.severe(now:Date())
        guard let change=transportPolicy.select(current:transport,wifi:wifiLastSeen,ble:bleLastSeen,now:Date(),
                                               voiceStarted:active && !ending && voiceMerge==nil ? voiceStarted:nil,audio:lastAudio,severeLoss:severe) else {return}
        transport=change.transport
        peer=transport == .wifi ? discovery.devices[device]?.endpoint:Self.bleEndpoint
        if transport != previous && authenticated && !paused && !token.isEmpty {
            voiceLog.info("Transport changed from=\(previous.rawValue,privacy:.public) to=\(self.transport.rawValue,privacy:.public) reason=\(change.reason,privacy:.public) recording=\(self.active) loss_pct=\(self.voiceLoss.percent)")
            beacon(broadcast:false);lastBeacon=Date()
        }
        voiceLoss.reset(now:Date())
    }
    private func sendOnAvailableLinks(_ body:[String:Any],type:UInt8=2) {
        if let found=discovery.devices[device],found.hasWiFi { _=send(body,type:type,to:found.endpoint) }
        if bluetooth?.isActive==true { _=send(body,type:type,viaBLE:true) }
    }
    func disconnectDevice() { paused=true;work.async {
        if self.authenticated { self.send(["type":"pc.disconnect","token":self.token,"device":self.device,"client":self.clientID]) }
        self.cancelConnectionRequest();self.paused=true;self.disconnect("已断开");self.connectionMessage="已断开";self.publish()
        if self.bluetooth?.isActive==true { self.bluetooth?.disconnect(after:0.5) { [weak self] in self?.paused==true } }
    } }
    func unpairDevice() { work.async {
        guard self.authenticated else { return };self.request("pc.unpair",extra:[:])
    } }
    @discardableResult private func send(_ body:[String:Any],type:UInt8=2,to target:sockaddr_in?=nil,viaBLE:Bool=false)->Bool {
        guard !powerSuspended || tearingDown else {return false}
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
        } else if !paused { body["device"]=device;body["client"]=clientID;body["primaryTransport"]=transport.rawValue;body["voiceV2"]=1;body["radioPCM"]=1;body["radioFormats"]=RadioFormat.supportedMask;sendOnAvailableLinks(body,type:3) }
    }
    private func sendStatus() {
        if !token.isEmpty { send(["type":"pc.status","token":token,"voiceV2":1,"radioPCM":1,"radioFormats":RadioFormat.supportedMask,"audioReady":AudioSink.deviceUID() != nil,"hotkeyReady":Shortcut.permitted,"epoch":Int(Date().timeIntervalSince1970),"tz":TimeZone.current.secondsFromGMT()/60,"idleHeartbeat":LANSocket.negotiatedHeartbeatSeconds]) }
    }
    func updateAgentQuotas(_ samples:[AgentQuotaSample]) { work.async {
        let next=Dictionary(uniqueKeysWithValues:samples.map{($0.agent,$0)})
        if self.authenticated,self.quotaCapable {
            for (agent,old) in self.quotaSamples where next[agent]==nil {
                self.send(["type":"quota.clear","token":self.token,"agent":agent.rawValue,"owner":old.owner,"sample":old.id])
            }
        }
        self.quotaSamples=next;self.flushQuotas()
    } }
    private func flushQuotas() {
        guard !powerSuspended,authenticated,quotaCapable,!paused,!active,Date().timeIntervalSince(lastSeen)<peerTimeout else{return}
        let now=Date()
        for (agent,sample) in quotaSamples {
            if let sent=quotaSent[agent],sent.0==sample.id,now.timeIntervalSince(sent.1)<30 {continue}
            guard var body=sample.wire(at:now) else{continue}
            body["type"]="quota.cache";body["token"]=token
            if send(body) {quotaSent[agent]=(sample.id,now)}
        }
    }
    func notifyAgent(_ event:AgentEvent) {
        work.async {
            guard !self.powerSuspended,self.authenticated,self.peer != nil,!self.token.isEmpty,Date().timeIntervalSince(self.lastSeen)<self.peerTimeout else { DispatchQueue.main.async{self.onAgentResult?("设备离线，本次提醒未发送")};return }
            guard self.agentCapable else { DispatchQueue.main.async{self.onAgentResult?("请先更新支持 Agent 提醒的设备固件")};return }
            guard self.agentPending.count<8 else { return }
            var body=event.deviceBody;body["type"]="agent.notify";body["id"]=event.id;body["token"]=self.token
            body["ttl_ms"]=10000;body["expires"]=Date().timeIntervalSince1970+10
            self.agentPending[event.id]=(body,Date().addingTimeInterval(5),Date(),1);self.send(body)
        }
    }
    private func tick() {
        guard !powerSuspended else { return }
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
        if active && now.timeIntervalSince(lastAudio)>ConnectionTiming.voiceTimeout { endVoice(immediate:true,reason:"audio-timeout") }
        else if active && now.timeIntervalSince(lastKey)>180 { endVoice(immediate:true,reason:"recording-limit") }
        else if active && !Shortcut.permitted { endVoice(immediate:true,reason:"permission-lost") }
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
        refreshWeather();flushQuotas();publish()
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
        guard !powerSuspended else {return}
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
            quotaCapable=body["agentQuota"] as? Int==1
            weatherCapable=body["weatherCache"] as? Int==1;agentCapable=body["agentNotify"] as? Int==1;slowHeartbeat=body["idleHeartbeat"] as? Int==30
            state.powerAvailable=body["powerDiagnostics"] as? Int==1
            if reconnect { recentEvents=[];settingsRevision=nil }
            bootID=body["boot"] as? Int
            let scope="\(device)/\(bootID ?? -1)"
            if voiceEventScope != scope {voiceEventScope=scope;voiceEvents=VoiceSessionEvents()}
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
            lastSeen=Date();if viaBLE { bleLastSeen=Date() } else { wifiLastSeen=Date() }
            // Accept a recovered audio packet before considering a stall.
            switch packet {case .audio,.voice:break;default:updateTransport()}
        }
        switch packet {
        case .ping:break
        case .audio(let seq,let pcm):
            guard authenticated,active,voiceMerge==nil else { return }
            var missing=0
            if let previous=sequence {
                let delta=seq &- previous
                if delta==0 || delta>128 { return }
                missing=Int(delta)-1
                if delta>1 { state.dropped+=Int(delta)-1;for _ in 0..<min(3,Int(delta)-1) { sink.append([Int16](repeating:0,count:1600)) } }
            }
            let received=Date()
            if state.packets==voiceInitialPackets { voiceLog.info("Voice first audio ms=\(Int(received.timeIntervalSince(self.voiceStarted)*1000)) transport=\(self.transport.rawValue,privacy:.public)") }
            else { voiceMaxGap=max(voiceMaxGap,received.timeIntervalSince(lastAudio)) }
            sequence=seq;state.packets+=1;lastAudio=received
            voiceLoss.record(missing:missing)
            if viaBLE {voiceBLEPackets+=1} else {voiceWiFiPackets+=1}
            sink.append(pcm)
        case .voice(let frame):
            guard authenticated,active,voiceMerge?.accept(frame,via:viaBLE ? .bluetooth:.wifi,now:ProcessInfo.processInfo.systemUptime)==true else {return}
            let received=Date()
            if state.packets==voiceInitialPackets {voiceLog.info("Voice first audio ms=\(Int(received.timeIntervalSince(self.voiceStarted)*1000)) mode=merge")}
            else {voiceMaxGap=max(voiceMaxGap,received.timeIntervalSince(lastAudio))}
            state.packets+=1;lastAudio=received
            if viaBLE {voiceBLEPackets+=1}else{voiceWiFiPackets+=1}
            if ending {endVoice(immediate:false,reason:"device-ended")}
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
        case "quota.reply":quotaAcceptanceReply?(body) // No front-end error state.
        case "agent.reply":
            guard let id=body["id"] as? String,agentPending.removeValue(forKey:id) != nil else { return }
            let ok=body["ok"] as? Bool==true
            DispatchQueue.main.async{self.onAgentResult?(ok ? "设备已接收提醒" : "设备未接受提醒")}
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
                    guard let self=self,!self.powerSuspended else{return};let now=Date()
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
            let session:UInt64?
            if body["voiceVersion"] != nil {
                guard let value=VoiceSessionEvents.session(body),let id=body["eid"] as? Int,voiceEvents.accept(id:id) else {return}
                session=value
                if active {endVoice(immediate:true,reason:"new-session")}
            } else {session=nil}
            stopWeather();stopRadioPlayback()
            if active && ending { endVoice(immediate:true) }
            guard authenticated,!token.isEmpty,!active else { return }
            do {
                try sink.start()
                guard !powerSuspended,try keyboard.start(shortcut) else {sink.stop();return}
                active=true;ending=false;voiceTailDeadline=nil;generation+=1;sequence=nil;lastAudio=Date();lastKey=Date();state.error=""
                voiceStarted=Date();voiceInitialPackets=state.packets;voiceInitialDropped=state.dropped;voiceMaxGap=0
                voiceWiFiPackets=0;voiceBLEPackets=0
                if let session {voiceMerge=VoiceMerge(session:session);startVoiceClock()}
                voiceLoss.reset(now:voiceStarted)
                voiceLog.info("Voice started transport=\(self.transport.rawValue,privacy:.public)")
            }
            catch { sink.stop();keyboard.stop();state.error=powerSuspended ? "" : error.localizedDescription }
        case "voice.end":
            if body["voiceVersion"] != nil {
                guard let session=VoiceSessionEvents.session(body),let id=body["eid"] as? Int,
                      let frames=body["frames"] as? Double,frames>=0,frames<=Double(VoiceFrame.maxFrames),frames.rounded()==frames,
                      voiceEvents.accept(id:id) else {return}
                guard voiceMerge?.session==session else {return}
                voiceMerge?.finish(frames:UInt32(frames))
            } else if voiceMerge != nil {return}
            endVoice(immediate:false,reason:"device-ended")
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
                if transport == .bluetooth { bluetooth?.disconnect(after:0.3) { [weak self] in self?.paused==true } }
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
        guard !powerSuspended,authenticated,!paused,weatherCapable,!active,radioPlayback==nil,weatherJob==nil,pending.isEmpty,Date()>=nextWeather,let location=weatherLocation else{return}
        nextWeather=Date().addingTimeInterval(60);weatherGeneration+=1;let expected=weatherGeneration
        weatherJob=WeatherFetch(location:location) { [weak self] result in self?.work.async { [weak self] in
            guard let self=self,!self.powerSuspended,self.weatherGeneration==expected,self.authenticated,self.weatherLocation==location else{return}
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
        guard !powerSuspended,authenticated,!paused,let playback=radioPlayback,var delivery=radioDelivery else { return }
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
    private func startVoiceClock() {
        voiceClock?.cancel()
        let clock=DispatchSource.makeTimerSource(queue:work)
        clock.schedule(deadline:.now(),repeating:.milliseconds(20),leeway:.milliseconds(3))
        clock.setEventHandler { [weak self] in
            guard let self=self,!self.powerSuspended,self.active else {return}
            let before=self.voiceMerge?.missing ?? 0
            guard let frames=self.voiceMerge?.drain(now:ProcessInfo.processInfo.systemUptime) else {return}
            self.state.dropped+=(self.voiceMerge?.missing ?? 0)-before
            for frame in frames {
                switch frame {
                case .audio(let frame,_):if let pcm=VoiceCodec.decode(frame.block) {self.sink.append(pcm)}
                case .silence:self.sink.append([Int16](repeating:0,count:1600))
                }
            }
        }
        voiceClock=clock;clock.resume()
    }
    private func endVoice(immediate:Bool,reason:String="session-ended") {
        guard active else { return }
        let now=ProcessInfo.processInfo.systemUptime
        if !ending {voiceTailDeadline=now+1}
        ending=true
        generation+=1;let expected=generation
        let stop={ [self] in
            guard generation==expected else { return }
            keyboard.stop();sink.stop();active=false;ending=false;sequence=nil
            voiceClock?.cancel();voiceClock=nil
            if let merge=voiceMerge {
                voiceLog.info("Voice merge accepted=\(merge.accepted) duplicate=\(merge.duplicates) late=\(merge.late) recovered=\(merge.recovered) missing=\(merge.missing) resync=\(merge.resyncs) wifi_copies=\(merge.wifiCopies) ble_copies=\(merge.bleCopies)")
            }
            voiceMerge=nil;voiceTailDeadline=nil
            voiceLog.info("Voice stopped packets=\(self.state.packets-self.voiceInitialPackets) dropped=\(self.state.dropped-self.voiceInitialDropped) max_gap_ms=\(Int(self.voiceMaxGap*1000)) immediate=\(immediate) reason=\(reason,privacy:.public) wifi_packets=\(self.voiceWiFiPackets) ble_packets=\(self.voiceBLEPackets) silence_ms=\(Int(Date().timeIntervalSince(self.lastAudio)*1000))")
            publish()
        }
        let grace=min(voiceMerge?.tailDelay(now:now) ?? 0.18,max(0,(voiceTailDeadline ?? now)-now))
        if immediate { stop() } else { work.asyncAfter(deadline:.now()+grace,execute:stop) }
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
        transportPolicy.reset()
        stopWeather();weatherLocation=nil;stopRadioPlayback()
        authenticated=false;pairing=false;connectionMessage="";slowHeartbeat=false;wifiLastSeen = .distantPast;bleLastSeen = .distantPast;state.powerAvailable=false;state.powerLoading=false
        agentPending.removeAll();agentCapable=false;quotaCapable=false;quotaSent.removeAll();
        cancelRadio(message)
        let interrupted = !pending.isEmpty
        pending.removeAll();peer=nil;state.address="";lastSeen = .distantPast;lastBeacon = .distantPast
        settingsRevision=nil;bootID=nil;recentEvents.removeAll();endVoice(immediate:true)
        if interrupted { DispatchQueue.main.async { self.onResult?(false,message) } }
    }
    static func testVoiceMerge(_ check:(Bool,String)->Void) {
        let engine=Engine(preview:true)
        engine.work.sync {
            engine.device="aabbccddeeff";engine.authenticated=true;engine.active=true
            var lan=sockaddr_in();lan.sin_family=sa_family_t(AF_INET);lan.sin_port=LANSocket.discoveryPort.bigEndian;lan.sin_addr.s_addr=inet_addr("192.0.2.20")
            _=engine.discovery.accept(["event":"device.hello","device":engine.device,"paired":true],from:lan)
            engine.voiceMerge=VoiceMerge(session:1)
            var wire=Data([6,1,0,0,0,0,0,0,0,0,0,0,0]);wire.append(Data(repeating:0,count:804))
            engine.processPacket(wire,from:lan,viaBLE:false)
            engine.processPacket(wire,from:Self.bleEndpoint,viaBLE:true)
            check(engine.state.packets==1 && engine.voiceMerge?.duplicates==1,"engine merges both authorized attachments before playback")
            let fresh=engine.lastAudio;wire[1]=2
            engine.processPacket(wire,from:lan,viaBLE:false)
            check(engine.state.packets==1 && engine.lastAudio==fresh,"old session cannot refresh fresh-audio watchdog")
            var legacy=Data([1,9,0x80]);legacy.append(Data(repeating:0,count:804))
            engine.processPacket(legacy,from:lan,viaBLE:false)
            check(engine.state.packets==1,"legacy audio cannot leak into a V2 recording")
            engine.handle(["event":"voice.end","voiceVersion":2,"voiceSession":"0000000000000002","frames":1.0,"eid":2])
            check(engine.active && !engine.ending,"old session end cannot stop current input")
            engine.handle(["event":"voice.end","voiceVersion":2,"voiceSession":"0000000000000001","frames":2.0,"eid":3])
            let limit=engine.voiceTailDeadline
            wire[1]=1;wire[9]=1;engine.processPacket(wire,from:lan,viaBLE:false)
            check(engine.ending && engine.voiceTailDeadline==limit,"late final audio can extend drain without moving its hard limit")
            engine.startVoiceClock();check(engine.voiceClock != nil,"active merge owns its playout timer")
            engine.endVoice(immediate:true,reason:"test-disconnect")
            check(!engine.active && engine.voiceMerge==nil && engine.voiceClock==nil && engine.voiceTailDeadline==nil,"disconnect releases merge timer and buffers immediately")
            wire[1]=1;engine.processPacket(wire,from:lan,viaBLE:false)
            check(!engine.active && engine.voiceMerge==nil,"late audio cannot automatically restart input")
        }
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
        check(engine.authenticated && engine.transport == .bluetooth && Date().timeIntervalSince(engine.wifiLastSeen)<1,"one authorized session keeps both attachments without preempting healthy BLE")
        let mirrored:[String:Any]=["event":"key.action","action":"test-no-input","eid":321]
        for _ in 0..<3 {feed(mirrored);feed(mirrored,ble:true)}
        check(engine.recentEvents.filter{$0==321}.count==1,"mirrored critical event retries enter the action path once across both links")
        engine.transport = .wifi;engine.transportPolicy.reset()
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
        switching.running=true
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
    static func testSleepLifecycle(_ check:(Bool,String)->Void) {
        let backlog=Engine(preview:true),entered=DispatchSemaphore(value:0),release=DispatchSemaphore(value:0)
        backlog.work.async {entered.signal();_=release.wait(timeout:.now()+5)}
        check(entered.wait(timeout:.now()+5) == .success,"create a stalled worker before system sleep")
        let before=ProcessInfo.processInfo.systemUptime
        backlog.suspendForSleep()
        check(ProcessInfo.processInfo.systemUptime-before<0.5 && backlog.powerSuspended,"sleep closes work admission without waiting for a stalled network queue")
        backlog.resumeAfterSleep();backlog.suspendForSleep();release.signal()
        backlog.work.sync {
            check(backlog.powerSuspended && !backlog.running && backlog.bluetooth==nil,"late resume behind a stalled worker cannot reconnect after another sleep")
        }
        let paused=Engine(preview:true,connectionPaused:true)
        check(paused.connectionPaused,"permission recovery applies disconnect intent before startup")
        let engine=Engine(preview:true)
        let receiver=try! LANSocket.open();defer{Darwin.close(receiver.descriptor)}
        engine.device="aabbccddeeff";engine.token=String(repeating:"a",count:64)
        engine.work.sync {
            engine.running=true
            engine.state.connected=true;engine.state.recording=true
            let clock=DispatchSource.makeTimerSource(queue:engine.work)
            clock.schedule(deadline:.now()+3600);clock.resume();engine.timer=clock
            let connection=try! LANSocket.open()
            engine.socketFD=connection.descriptor
            let source=DispatchSource.makeReadSource(fileDescriptor:connection.descriptor,queue:engine.work)
            source.setCancelHandler{Darwin.close(connection.descriptor)};source.resume();engine.reader=source
            var endpoint=Self.bleEndpoint;endpoint.sin_port=receiver.port.bigEndian
            engine.peer=endpoint;engine.authenticated=true
            engine.radioTransfer=try! RadioListTransfer(stations:[],revision:1,capacity:20)
            engine.handle(["event":"radio.open","session":"0123456789abcdef","url":"http://127.0.0.1:9/absent","window":10])
            check(engine.radioActivity != nil && engine.radioClock != nil && engine.radioPlayback != nil,"sleep cleanup starts with a live radio activity and decoder")
        }
        engine.suspendForSleep()
        engine.start();engine.retry();engine.retryBluetooth()
        engine.work.sync {
            let discoveryAt=engine.lastDiscovery
            engine.tick()
            check(engine.lastDiscovery==discoveryAt,"a queued timer callback cannot perform discovery after sleep")
            check(engine.powerSuspended && !engine.running && engine.bluetooth==nil && engine.timer==nil && engine.socketFD<0,"late startup and retry requests cannot restart a sleeping engine")
            check(engine.device=="aabbccddeeff" && engine.token==String(repeating:"a",count:64) && !engine.paused,"sleep retains authorization and the user's reconnect preference")
            check(engine.radioClock==nil && engine.radioActivity==nil && !engine.active && !engine.sink.running,"sleep releases audio timing, playback and keyboard capture")
            check(!engine.state.connected && !engine.state.recording,"sleep cleanup removes stale Connected/Recording UI state")
        }
        var bytes=[UInt8](repeating:0,count:2048)
        var readable=pollfd(fd:receiver.descriptor,events:Int16(POLLIN),revents:0)
        _=Darwin.poll(&readable,1,500)
        let received=Darwin.recv(receiver.descriptor,&bytes,bytes.count,0)
        let packet=received>1 ? Data(bytes[1..<received]):Data()
        let command=(try? JSONSerialization.jsonObject(with:packet)) as? [String:Any]
        check(command?["type"] as? String=="radio.abort","sleep cancels a pending station-list write before closing its transport")
        engine.suspendForSleep();engine.stop();engine.start()
        engine.work.sync {
            check(engine.powerSuspended && !engine.running && engine.timer==nil,"repeated stop or permission reload cannot bypass sleep suspension")
        }
    }
    func suspendForSleep() {
        // Close admission on the caller immediately. Slow audio/driver cleanup
        // runs on its owning queue and never blocks the system sleep callback.
        lifecycle.suspend()
        // Release held keys before acknowledging system sleep. Deferring this
        // to a later maintenance wake could itself synthesize user activity.
        keyboard.suspendForSleep()
        work.async {self.stopNow()}
    }
    func resumeAfterSleep() {
        let request=lifecycle.requestResume()
        work.async {
            guard self.lifecycle.completeResume(request) else {return}
            self.startNow()
        }
    }
    func stop(completion:(()->Void)?=nil) {
        if let completion=completion {work.async {self.stopNow(completion:completion)}}
        else {work.sync {stopNow()}}
    }
    private func stopNow(completion:(()->Void)?=nil) {
        tearingDown=true;defer{tearingDown=false}
        running=false
        cancelRadio("连接已结束，未确认的修改请重新读取")
        if authenticated { sendOnAvailableLinks(["type":"pc.disconnect","token":token,"device":device,"client":clientID]) }
        cancelConnectionRequest()
        // Release radio links before potentially slow Core Audio disposal.
        bluetooth?.shutdown();bluetooth=nil
        timer?.cancel();timer=nil;socketGeneration+=1
        if let reader=reader {
            let fd=socketFD
            // Reader callbacks share this serial queue. Invalidate their
            // generation first, then close here; the old cancel handler must
            // not later close an unrelated descriptor that reused this number.
            reader.setCancelHandler {}
            reader.cancel()
            if fd>=0 {Darwin.close(fd)}
        }
        reader=nil;socketFD = -1;localPort=0
        disconnect("连接已结束，未确认的修改请重新读取")
        keyboard.stop();sink.stop()
        publish()
        if let completion=completion {DispatchQueue.main.async(execute:completion)}
    }
}

// Explicit acceptance command. Reuses saved authorization and never pairs or
// alters preferences. Test observations are removed before returning control.
extension Engine {
    static func runQuotaDeviceTest(ble:Bool)->Int32 {
        guard let id=UserDefaults.standard.string(forKey:"device"),PairingStore.load(id) != nil else{return 2}
        setbuf(stdout,nil)
        let engine=Engine(lanEnabled:!ble),clock=DispatchSource.makeTimerSource(queue:.main)
        let deadline=Date().addingTimeInterval(50),sample=UUID().uuidString,newSample=UUID().uuidString
        let stamp=Int(Date().timeIntervalSince1970*1000)
        var armed=false,done=false,step=0,baseline=0
        func send(_ body:[String:Any]) {_=engine.send(body.merging(["token":engine.token]){a,_ in a},viaBLE:ble)}
        func clear(_ value:String) {send(["type":"quota.clear","agent":"quota-test","owner":"local-acceptance","sample":value])}
        func fixture(_ value:String,_ ttl:Int,_ version:Int)->[String:Any] {
            ["type":"quota.cache","schema":1,"agent":"quota-test","name":"Quota test","owner":"local-acceptance","sample":value,"stamp":version,"ttl_ms":ttl,"age_ms":0,
             "items":[["kind":1,"label":"Weekly","remaining":4600,"minutes":10080,"reset_ms":100000]]]
        }
        func finish(_ success:Bool) {
            guard !done else{return};done=true;clock.cancel();clear(sample);clear(newSample)
            print("Quota \(ble ? "BLE-only":"LAN") acceptance: \(success ? "PASS":"FAIL")")
            DispatchQueue.main.asyncAfter(deadline:.now()+0.4) {engine.stop {exit(success ? 0:1)}}
        }
        engine.quotaAcceptanceReply={body in
            guard !done,let pages=body["pages"] as? Int else{return}
            switch step {
            case 0:
                baseline=pages;step=1;send(fixture(sample,2500,stamp))
            case 1:
                guard body["ok"] as? Bool==true,pages==baseline+1 else{finish(false);return}
                print("Quota accepted over \(ble ? "BLE-only":"LAN")");step=2
                engine.work.asyncAfter(deadline:.now()+1) {guard !done else{return};send(fixture(sample,2500,stamp))}
            case 2:
                guard body["ok"] as? Bool==true else{finish(false);return};step=3
                engine.work.asyncAfter(deadline:.now()+1.8) {guard !done else{return};send(["type":"quota.get"])}
            case 3:
                guard pages==baseline else{finish(false);return}
                print("Duplicate did not renew TTL; expiry verified");step=4;send(fixture(newSample,30000,stamp+1))
            case 4:
                guard body["ok"] as? Bool==true,pages==baseline+1 else{finish(false);return};step=5;clear(sample)
            case 5:
                guard body["ok"] as? Bool==false,pages==baseline+1 else{finish(false);return}
                print("Delayed clear retained newer observation");step=6;clear(newSample)
            case 6:finish(body["ok"] as? Bool==true && pages==baseline)
            default:finish(false)
            }
        }
        engine.onState={state in
            guard !armed,state.connected,ble ? state.bleConnected:state.wifiConnected else{return}
            armed=true;engine.work.async {guard engine.quotaCapable else{finish(false);return};send(["type":"quota.get"])}
        }
        clock.schedule(deadline:.now(),repeating:1)
        clock.setEventHandler {if Date()>=deadline{engine.work.async{finish(false)}}}
        clock.resume();engine.start();dispatchMain()
    }
}
