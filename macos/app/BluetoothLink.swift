import Foundation
import CoreBluetooth
import os

struct BLEFrame {
    private var data=Data(),identifier:UInt8=0,total=0,started=Date.distantPast
    mutating func accept(_ fragment:Data,now:Date=Date())->Data? {
        let bytes=Array(fragment)
        guard bytes.count>7,bytes[0]==0x56,bytes[2]&0xfc==0 else { data=Data();total=0;return nil }
        let length=Int(bytes[3])|Int(bytes[4])<<8,offset=Int(bytes[5])|Int(bytes[6])<<8
        guard length>0,length<=1537,offset+bytes.count-7<=length else { data=Data();total=0;return nil }
        if bytes[2]&1 != 0 { identifier=bytes[1];total=length;data=Data();started=now }
        guard identifier==bytes[1],total==length,offset==data.count,now>=started,now.timeIntervalSince(started)<=2 else { data=Data();total=0;return nil }
        data.append(contentsOf:bytes.dropFirst(7))
        if bytes[2]&2 != 0 { let result=data.count==total ? data : nil;data=Data();total=0;return result }
        return nil
    }
    static func fragments(_ packet:Data,identifier:UInt8,limit:Int,media:Bool=false)->[Data] {
        guard !packet.isEmpty,packet.count<=1537,limit>7 else { return [] }
        let bytes=Array(packet),step=limit-7
        return stride(from:0,to:bytes.count,by:step).map { offset in
            let end=min(offset+step,bytes.count)
            var frame=Data([0x56,identifier,(offset==0 ? 1:0)|(end==bytes.count ? 2:0)|(media ? 4:0),UInt8(bytes.count&255),UInt8(bytes.count>>8),UInt8(offset&255),UInt8(offset>>8)])
            frame.append(contentsOf:bytes[offset..<end]);return frame
        }
    }
}

final class BluetoothLink:NSObject,CBCentralManagerDelegate,CBPeripheralDelegate {
    private let log=Logger(subsystem:"io.github.vibecompanion.mac",category:"bluetooth")
    static let service=CBUUID(string:"71DF1000-7B51-4A1D-A9B3-5A7669626532")
    private let infoID=CBUUID(string:"71DF1001-7B51-4A1D-A9B3-5A7669626532")
    private let rxID=CBUUID(string:"71DF1002-7B51-4A1D-A9B3-5A7669626532")
    private let txID=CBUUID(string:"71DF1003-7B51-4A1D-A9B3-5A7669626532")
    private var central:CBCentralManager!,peripherals:[String:CBPeripheral]=[:]
    private var selected:CBPeripheral?,device="",rx:CBCharacteristic?,info:CBCharacteristic?
    private var radioNextWrite=Date.distantPast,radioWriteScheduled=false
    var radioWithoutResponse:Bool {rx?.properties.contains(.writeWithoutResponse) ?? false}
    var queuedWrites:Int {frames.count}
    private var notificationReady=false,lastReceived=Date.distantPast
    private var enabled=true
    private var retries:[String:BLEConnectionRetry]=[:]
    private var writeLogCount=0,readLogCount=0,frameLogCount=0,packetLogCount=0
    private var frames=BLEWriteBuffer(),writing=false,sequence:UInt8=0,decoder=BLEFrame(),generation=0
    private var writeRetry=BLEWriteRetry()
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
        selected=nil;rx=nil;info=nil;frames.reset();writing=false;decoder=BLEFrame()
        writeRetry.reset()
        notificationReady=false;lastReceived = .distantPast
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
    @discardableResult func send(_ packet:Data)->Bool {
        guard let peripheral=selected,let rx=rx,peripheral.state == .connected else { return false }
        sequence &+= 1
        let parts=BLEFrame.fragments(packet,identifier:sequence,limit:min(244,peripheral.maximumWriteValueLength(for:.withoutResponse)),media:packet.first==5)
        guard frames.append(parts,radio:packet.first==5,key:packet.first==5 ? Data(packet.prefix(13)):nil) else { return false };writeNext(peripheral,rx);return true
    }
    func discardRadio() { frames.discardRadio(writing:writing) }
    func acknowledgeRadio(session:UInt64,next:UInt32,mask:UInt16) {
        frames.acknowledgeRadio(session:session,next:next,mask:mask,writing:writing)
        if let peripheral=selected,let rx=rx {writeNext(peripheral,rx)}
    }
    private func writeNext(_ peripheral:CBPeripheral,_ characteristic:CBCharacteristic) {
        guard enabled,!writing else { return }
        while let frame=frames.first {
            if frames.firstIsRadio,Date()<radioNextWrite {
                if !radioWriteScheduled {
                    radioWriteScheduled=true;let expected=generation
                    queue.asyncAfter(deadline:.now()+max(0,radioNextWrite.timeIntervalSinceNow)) { [weak self,weak peripheral] in
                        guard let self=self else {return};self.radioWriteScheduled=false
                        guard let peripheral=peripheral,self.generation==expected,let rx=self.rx else {return};self.writeNext(peripheral,rx)
                    }
                }
                return
            }
            if frames.firstIsRadio && characteristic.properties.contains(.writeWithoutResponse) {
                guard peripheral.canSendWriteWithoutResponse else { return }
                peripheral.writeValue(frame,for:characteristic,type:.withoutResponse);frames.acknowledge()
                if frame[2]&2 != 0 {radioNextWrite=Date().addingTimeInterval(0.05)}
            } else {
                writing=true;peripheral.writeValue(frame,for:characteristic,type:.withResponse);return
            }
        }
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
        generation+=1;notificationReady=false;rx=nil;info=nil;frames.reset();writing=false;decoder=BLEFrame();writeRetry.reset()
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
                notificationReady=true;lastReceived=Date();watchSilence(generation)
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
        guard peripheral.identifier==selected?.identifier,characteristic === rx else { return }
        if let error=error {
            let e=error as NSError
            log.error("BLE write failed domain=\(e.domain,privacy:.public) code=\(e.code) message=\(e.localizedDescription,privacy:.public)")
            if Self.isWriteCongestion(error),let delay=writeRetry.delay() {
                let expected=generation
                log.notice("BLE write congested; retry=\(self.writeRetry.attempts) delay_ms=\(Int(delay*1000)) queued=\(self.frames.count)")
                // Keep writing=true and retain the first fragment. Other
                // producers cannot bypass backoff or overwrite this write.
                queue.asyncAfter(deadline:.now()+delay) { [weak self,weak peripheral] in
                    guard let self=self,self.enabled,self.generation==expected,
                          let peripheral=peripheral,peripheral.identifier==self.selected?.identifier,let rx=self.rx else {return}
                    self.writing=false;self.writeNext(peripheral,rx)
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
        frames.acknowledge();writing=false;writeRetry.reset()
        if let rx=rx { writeNext(peripheral,rx) }
    }
}
