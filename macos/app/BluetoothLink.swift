import Foundation
import CoreBluetooth
import os

final class BluetoothLink:NSObject,CBCentralManagerDelegate,CBPeripheralDelegate {
    private let log=Logger(subsystem:"io.github.vibecompanion.mac",category:"bluetooth")
    static let service=CBUUID(string:"71DF1000-7B51-4A1D-A9B3-5A7669626532")
    private let infoID=CBUUID(string:"71DF1001-7B51-4A1D-A9B3-5A7669626532")
    private let rxID=CBUUID(string:"71DF1002-7B51-4A1D-A9B3-5A7669626532")
    private let txID=CBUUID(string:"71DF1003-7B51-4A1D-A9B3-5A7669626532")
    private var central:CBCentralManager!,peripherals:[String:CBPeripheral]=[:]
    private var selected:CBPeripheral?,device="",rx:CBCharacteristic?,info:CBCharacteristic?
    private var pacing=BLEWritePacing(),writeWake:DispatchSourceTimer?
    var radioWithoutResponse:Bool {rx?.properties.contains(.writeWithoutResponse) ?? false}
    var queuedWrites:Int {frames.count}
    private var notificationReady=false,lastReceived=Date.distantPast
    private var enabled=true
    private var retries:[String:BLEConnectionRetry]=[:]
    private var writeLogCount=0,readLogCount=0,frameLogCount=0,packetLogCount=0
    private var frames=BLEWriteBuffer(),flight=BLEWriteFlight(),sequence:UInt8=0,decoder=BLEFrame(),generation=0
    private var writeRetry=BLEWriteRetry()
    private var repairSequence:UInt64=0
    private var repairCompletion:(id:UInt64,callback:VoiceRepairCompletion)?
    var isActive:Bool { notificationReady && selected?.state == .connected }
    private let queue:DispatchQueue
    var onFound:((String,UUID,Bool)->Void)?
    var onPacket:((String,Data)->Void)?
    var onReady:(()->Void)?
    var onDisconnected:((String)->Void)?
    var onAuthenticationFailed:((String)->Void)?
    var onStatus:((String)->Void)?
    var onPairingRepairChanged:((String,Bool)->Void)?
    static func isPairingRecordLost(_ error:Error?)->Bool {
        guard let error=error as NSError? else { return false }
        return error.domain==CBErrorDomain && error.code==CBError.Code.peerRemovedPairingInformation.rawValue
    }
    static func isWriteCongestion(_ error:Error)->Bool {
        let e=error as NSError
        return e.domain==CBATTErrorDomain && [CBATTError.Code.insufficientResources.rawValue,CBATTError.Code.prepareQueueFull.rawValue].contains(e.code)
    }
    static func isWriteAuthenticationError(_ error:Error)->Bool {
        let e=error as NSError
        return e.domain==CBATTErrorDomain && [CBATTError.Code.insufficientAuthentication.rawValue,CBATTError.Code.insufficientEncryption.rawValue,CBATTError.Code.insufficientAuthorization.rawValue].contains(e.code)
    }
    init(queue:DispatchQueue) { self.queue=queue;super.init();central=CBCentralManager(delegate:self,queue:queue) }
    func scan() {
        guard enabled else { return }
        if central.state == .poweredOn && !central.isScanning { central.scanForPeripherals(withServices:[Self.service],options:[CBCentralManagerScanOptionAllowDuplicatesKey:true]) }
    }
    func connect(_ id:String,resetBackoff:Bool=false) {
        guard enabled else { return }
        guard let peripheral=peripherals[id] else { return }
        if resetBackoff { retries[id]=BLEConnectionRetry();onPairingRepairChanged?(id,false) }
        if selected?.identifier==peripheral.identifier { return }
        guard peripheral.state != .disconnecting else { return }
        guard retries[id]?.allows(Date()) ?? true else { return }
        disconnect();device=id;selected=peripheral;peripheral.delegate=self;generation+=1;let expected=generation
        log.info("Starting BLE connection peripheral=\(peripheral.identifier.uuidString,privacy:.public)");onStatus?("正在连接蓝牙…");central.connect(peripheral)
        queue.asyncAfter(deadline:.now()+20) { [weak self] in
            guard let self=self,self.generation==expected,!self.notificationReady else { return }
            self.failed("蓝牙连接超时，稍后自动重试")
        }
    }
    func disconnect(caller:String=#function) {
        if selected != nil { log.info("Cancelling BLE link caller=\(caller,privacy:.public)") }
        generation+=1
        if let selected=selected { central.cancelPeripheralConnection(selected) }
        selected=nil;rx=nil;info=nil;decoder=BLEFrame()
        notificationReady=false;lastReceived = .distantPast
        resetWrites()
    }
    func disconnect(after delay:TimeInterval,when allowed:@escaping ()->Bool) {
        let expected=generation
        queue.asyncAfter(deadline:.now()+delay) { [weak self] in
            guard let self=self,self.enabled,self.generation==expected,allowed() else {return}
            self.disconnect()
        }
    }
    func shutdown() {
        // Cancel discovery as well as the selected peripheral. Otherwise an
        // advertisement already queued during sleep can reconnect the device.
        enabled=false;central.stopScan();central.delegate=nil
        selected?.delegate=nil
        disconnect()
        onFound=nil;onPacket=nil;onReady=nil;onDisconnected=nil
        onAuthenticationFailed=nil;onStatus=nil;onPairingRepairChanged=nil
        peripherals.removeAll()
    }
    private func failed(_ message:String,pairingLost:Bool=false) {
        let id=device;disconnect();retries[id,default:BLEConnectionRetry()].failed(Date())
        if pairingLost { retries[id,default:BLEConnectionRetry()].requirePairingRepair();onPairingRepairChanged?(id,true) }
        log.info("BLE retry delayed failures=\(self.retries[id]?.failures ?? 0)")
        onStatus?(message);onDisconnected?(message)
    }
    private func watchSilence(_ expected:Int) {
        queue.asyncAfter(deadline:.now()+1) { [weak self] in
            guard let self=self,self.generation==expected,self.notificationReady else { return }
            if Date().timeIntervalSince(self.lastReceived)>ConnectionTiming.bleSilenceTimeout {
                self.failed("蓝牙连接无响应，稍后自动重试")
            } else { self.watchSilence(expected) }
        }
    }
    @discardableResult func send(_ packet:Data,voice:Bool=false)->Bool {
        guard let peripheral=selected,let rx=rx,peripheral.state == .connected else { return false }
        sequence &+= 1
        let radio=packet.first==5
        // Keep one write kind for control and repairs so CoreBluetooth cannot
        // interleave their assemblies. Credits bound fragments; pacing below
        // separately bounds production of complete background requests.
        let kind:CBCharacteristicWriteType=rx.properties.contains(.writeWithoutResponse) ? .withoutResponse:.withResponse
        // With-response reports long-write capacity on some peers. Keep each
        // application fragment inside one ATT value rather than relying on
        // Prepare/Execute Write support in the peripheral.
        let limit=min(244,peripheral.maximumWriteValueLength(for:.withoutResponse),peripheral.maximumWriteValueLength(for:kind))
        let parts=BLEFrame.fragments(packet,identifier:sequence,limit:limit,media:radio)
        guard frames.append(parts,radio:radio,key:radio ? Data(packet.prefix(13)):nil,voice:voice,feedback:packet.first==7,writing:flight.inFlight) else { return false }
        writeNext(peripheral,rx);return true
    }
    func sendVoiceRepair(_ packet:Data,completion:@escaping VoiceRepairCompletion) {
        guard let peripheral=selected,let rx,peripheral.state == .connected else {
            completion(.notSubmitted,ProcessInfo.processInfo.systemUptime);return
        }
        sequence &+= 1;repairSequence &+= 1
        let limit=min(244,peripheral.maximumWriteValueLength(for:.withoutResponse),peripheral.maximumWriteValueLength(for:.withResponse))
        let parts=BLEFrame.fragments(packet,identifier:sequence,limit:limit)
        let repair=BLEWriteBuffer.Repair(id:repairSequence,deadline:ProcessInfo.processInfo.systemUptime+VoicePolicy.repairQueueLifetime)
        guard frames.append(parts,radio:false,repair:repair,writing:flight.inFlight) else {
            completion(.notSubmitted,ProcessInfo.processInfo.systemUptime);return
        }
        repairCompletion=(repair.id,completion)
        let expected=generation
        queue.asyncAfter(deadline:.now()+VoicePolicy.repairQueueLifetime) { [weak self] in
            guard let self,self.enabled,self.generation==expected,self.repairCompletion?.id==repair.id else {return}
            let expired=self.frames.discardExpiredRepairs(now:ProcessInfo.processInfo.systemUptime,writing:self.flight.inFlight)
            self.applyRemoval(expired)
            if let peripheral=self.selected,let rx=self.rx {self.writeNext(peripheral,rx)}
        }
        writeNext(peripheral,rx)
    }
    private func finishRepairs(_ results:[BLEWriteBuffer.RepairResult]) {
        for result in results {
            guard let pending=repairCompletion,pending.id==result.id else {continue}
            repairCompletion=nil
            pending.callback(result.submission,ProcessInfo.processInfo.systemUptime)
        }
    }
    private func resetWrites() {
        let repairs=frames.reset(writing:flight.inFlight)
        flight.reset();writeRetry.reset();pacing=BLEWritePacing()
        writeWake?.cancel();writeWake=nil;finishRepairs(repairs)
    }
    private func applyRemoval(_ removed:BLEWriteBuffer.Removal) {
        if removed.headRemoved {flight.headRemoved();writeRetry.reset()}
        finishRepairs(removed.repairs)
    }
    func discardRadio() {
        let removed=frames.discardRadio(writing:flight.inFlight);applyRemoval(removed)
        if let peripheral=selected,let rx {writeNext(peripheral,rx)}
    }
    func discardVoiceRepair() {
        let removed=frames.discardVoiceRepair(writing:flight.inFlight);applyRemoval(removed)
        if let peripheral=selected,let rx {writeNext(peripheral,rx)}
    }
    func acknowledgeRadio(session:UInt64,next:UInt32,mask:UInt16) {
        let removed=frames.acknowledgeRadio(session:session,next:next,mask:mask,writing:flight.inFlight);applyRemoval(removed)
        if let peripheral=selected,let rx=rx {writeNext(peripheral,rx)}
    }
    private func writeNext(_ peripheral:CBPeripheral,_ characteristic:CBCharacteristic) {
        guard enabled,!flight.busy else { return }
        let expired=frames.discardExpiredRepairs(now:ProcessInfo.processInfo.systemUptime)
        applyRemoval(expired)
        while !frames.isEmpty {
            let now=ProcessInfo.processInfo.systemUptime
            let budget=pacing
            frames.selectReady{budget.delay($0,now:now)==0}
            guard let frame=frames.first,let kind=frames.firstKind else {return}
            let delay=pacing.delay(kind,now:now)
            if delay>0 {wakeWrites(after:delay);return}
            if characteristic.properties.contains(.writeWithoutResponse) {
                guard peripheral.canSendWriteWithoutResponse else { return }
                peripheral.writeValue(frame,for:characteristic,type:.withoutResponse)
                if let repair=frames.acknowledge() {finishRepairs([repair])}
                if frame[2]&2 != 0 {pacing.completed(kind,now:ProcessInfo.processInfo.systemUptime)}
            } else {
                flight.begin();peripheral.writeValue(frame,for:characteristic,type:.withResponse);return
            }
        }
    }
    private func wakeWrites(after delay:TimeInterval) {
        if writeWake==nil {
            let timer=DispatchSource.makeTimerSource(queue:queue),expected=generation
            timer.setEventHandler { [weak self] in
                guard let self,self.enabled,self.generation==expected else {return}
                self.writeWake?.cancel();self.writeWake=nil
                if let peripheral=self.selected,let rx=self.rx {self.writeNext(peripheral,rx)}
            }
            writeWake=timer;timer.resume()
        }
        // Re-arm for the current head, including a newly eligible radio packet.
        writeWake?.schedule(deadline:.now()+delay,leeway:.milliseconds(1))
    }
    func centralManagerDidUpdateState(_ central:CBCentralManager) {
        guard enabled else { return }
        switch central.state {
        case .poweredOn:onStatus?("");scan()
        case .poweredOff:onStatus?("蓝牙已关闭")
        case .unauthorized:onStatus?("请允许 Vibe 伴侣使用蓝牙")
        case .unsupported:onStatus?("这台电脑不支持 BLE")
        default:onStatus?("蓝牙正在准备")
        }
        if central.state != .poweredOn { disconnect() }
    }
    func centralManager(_ central:CBCentralManager,didDiscover peripheral:CBPeripheral,advertisementData:[String:Any],rssi RSSI:NSNumber) {
        guard enabled else { return }
        let name=advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? peripheral.name ?? ""
        guard name.hasPrefix("Vibe-") else { return }
        let id=String(name.dropFirst(5)).lowercased()
        guard id.count==12,id.allSatisfy({"0123456789abcdef".contains($0)}) else { return }
        guard peripherals[id] != nil || peripherals.count<16 else { return }
        peripherals[id]=peripheral
        onFound?(id,peripheral.identifier,advertisementData[CBAdvertisementDataIsConnectable] as? Bool ?? true)
    }
    func centralManager(_ central:CBCentralManager,didConnect peripheral:CBPeripheral) {
        guard peripheral.identifier==selected?.identifier else { return }
        log.info("BLE connected; discovering service");peripheral.discoverServices([Self.service])
    }
    func centralManager(_ central:CBCentralManager,didFailToConnect peripheral:CBPeripheral,error:Error?) {
        guard peripheral.identifier==selected?.identifier else { return }
        if let error=error as NSError? { log.error("BLE connect failed peripheral=\(peripheral.identifier.uuidString,privacy:.public) domain=\(error.domain,privacy:.public) code=\(error.code)") }
        let lost=Self.isPairingRecordLost(error)
        failed(lost ? "蓝牙配对记录已失效" : "蓝牙连接失败，稍后自动重试",pairingLost:lost)
    }
    func centralManager(_ central:CBCentralManager,didDisconnectPeripheral peripheral:CBPeripheral,error:Error?) {
        guard peripheral.identifier==selected?.identifier,peripheral.state == .disconnected else { return }
        if let error=error as NSError? { log.error("BLE disconnected domain=\(error.domain,privacy:.public) code=\(error.code)") }
        let lost=Self.isPairingRecordLost(error)
        failed(lost ? "蓝牙配对记录已失效" : "蓝牙连接已断开，正在重连",pairingLost:lost)
    }
    func peripheral(_ peripheral:CBPeripheral,didDiscoverServices error:Error?) {
        guard peripheral.identifier==selected?.identifier else { return }
        guard error==nil,let service=peripheral.services?.first(where:{$0.uuid==Self.service}) else { failed("设备的蓝牙服务不可用");return }
        log.info("BLE service found; discovering characteristics");peripheral.discoverCharacteristics([infoID,rxID,txID],for:service)
    }
    func peripheral(_ peripheral:CBPeripheral,didModifyServices invalidatedServices:[CBService]) {
        guard peripheral.identifier==selected?.identifier,invalidatedServices.contains(where:{$0.uuid==Self.service}) else {return}
        // A firmware update can change properties while the encrypted bond is
        // retained. Rediscover without forgetting the device or its authority.
        generation+=1;notificationReady=false;rx=nil;info=nil;decoder=BLEFrame();resetWrites()
        onDisconnected?("蓝牙服务正在更新")
        peripheral.discoverServices([Self.service])
    }
    func peripheral(_ peripheral:CBPeripheral,didDiscoverCharacteristicsFor service:CBService,error:Error?) {
        guard peripheral.identifier==selected?.identifier else { return }
        let chars=service.characteristics ?? []
        rx=chars.first{$0.uuid==rxID};info=chars.first{$0.uuid==infoID}
        guard error==nil,rx != nil,let info=info,let tx=chars.first(where:{$0.uuid==txID}) else { failed("设备蓝牙协议不完整，请更新固件");return }
        log.info("BLE characteristics found radio_no_response=\(self.radioWithoutResponse); reading info and subscribing");peripheral.readValue(for:info);peripheral.setNotifyValue(true,for:tx)
    }
    func peripheral(_ peripheral:CBPeripheral,didUpdateNotificationStateFor characteristic:CBCharacteristic,error:Error?) {
        guard peripheral.identifier==selected?.identifier else { return }
        if error != nil { failed("无法订阅蓝牙数据，稍后自动重试") }
        else if characteristic.isNotifying {
            if !notificationReady {
                notificationReady=true;lastReceived=Date();watchSilence(generation);onStatus?("")
                log.info("BLE notifications ready");onReady?()
            }
        } else { failed("蓝牙通知已停止，稍后自动重试") }
    }
    func peripheral(_ peripheral:CBPeripheral,didUpdateValueFor characteristic:CBCharacteristic,error:Error?) {
        guard peripheral.identifier==selected?.identifier,error==nil,let data=characteristic.value else { return }
        if readLogCount<6 { log.info("BLE received characteristic=\(characteristic.uuid.uuidString,privacy:.public) bytes=\(data.count)");readLogCount+=1 }
        if characteristic.uuid==infoID {
            lastReceived=Date()
            var packet=Data([2]);packet.append(data);onPacket?(device,packet)
        }
        else if characteristic.uuid==txID {
            let packet=decoder.accept(data)
            if frameLogCount<10 {
                let header=data.prefix(7).map{String(format:"%02x",$0)}.joined()
                log.info("BLE frame header=\(header,privacy:.public) complete=\(packet?.count ?? 0)");frameLogCount+=1
            }
            if let packet=packet {
                lastReceived=Date()
                retries[device]=BLEConnectionRetry()
                if packetLogCount<6 {
                    let body=(try? JSONSerialization.jsonObject(with:packet.dropFirst())) as? [String:Any]
                    let event=body?["event"] as? String ?? "invalid",auth=body?["authorized"] as? Bool ?? false
                    log.info("BLE packet type=\(packet.first ?? 0) event=\(event,privacy:.public) authorized=\(auth)");packetLogCount+=1
                }
                onPacket?(device,packet)
            }
        }
    }
    func peripheralIsReady(toSendWriteWithoutResponse peripheral:CBPeripheral) {
        guard peripheral.identifier==selected?.identifier,let rx=rx else { return };writeNext(peripheral,rx)
    }
    func peripheral(_ peripheral:CBPeripheral,didWriteValueFor characteristic:CBCharacteristic,error:Error?) {
        guard peripheral.identifier==selected?.identifier,characteristic === rx,flight.inFlight else { return }
        if frames.firstCancelled {
            frames.acknowledge();flight.complete();writeRetry.reset()
            if let rx {writeNext(peripheral,rx)}
            return
        }
        if let error=error {
            let e=error as NSError
            log.error("BLE write failed domain=\(e.domain,privacy:.public) code=\(e.code) message=\(e.localizedDescription,privacy:.public)")
            if Self.isWriteCongestion(error),let delay=writeRetry.delay() {
                let expected=generation,attempt=flight.deferRetry()
                log.notice("BLE write congested; retry=\(self.writeRetry.attempts) delay_ms=\(Int(delay*1000)) queued=\(self.frames.count)")
                // Retain the packet, but no ATT fragment is in flight now.
                // Cancellation may remove it and invalidate this timer.
                queue.asyncAfter(deadline:.now()+delay) { [weak self,weak peripheral] in
                    guard let self=self,self.enabled,self.generation==expected,
                          let peripheral=peripheral,peripheral.identifier==self.selected?.identifier,let rx=self.rx,
                          self.flight.resume(attempt) else {return}
                    self.writeNext(peripheral,rx)
                }
                return
            }
            let authentication=Self.isWriteAuthenticationError(error),lost=Self.isPairingRecordLost(error)
            let message=lost ? "蓝牙配对记录已失效":authentication ? "蓝牙安全连接未完成，请重试":"蓝牙数据发送失败，正在重新连接"
            failed(message,pairingLost:lost)
            if authentication {onAuthenticationFailed?(message)}
            return
        }
        if writeLogCount<6 { log.info("BLE write acknowledged bytes=\(self.frames.first?.count ?? 0)");writeLogCount+=1 }
        onStatus?("")
        if let kind=frames.firstKind,let frame=frames.first,frame[2]&2 != 0 {
            pacing.completed(kind,now:ProcessInfo.processInfo.systemUptime)
        }
        let repair=frames.acknowledge();flight.complete();writeRetry.reset()
        if let repair {finishRepairs([repair])}
        if let rx=rx { writeNext(peripheral,rx) }
    }
}
