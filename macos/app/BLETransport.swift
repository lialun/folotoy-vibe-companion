import Foundation
import CoreBluetooth
import os

enum BLEWire {
    static let service=CBUUID(string:"6F6C6F56-6962-436F-8D11-0B4DE2C07400")
    static let info=CBUUID(string:"6F6C6F56-6962-436F-8D11-0B4DE2C07401")
    static let rx=CBUUID(string:"6F6C6F56-6962-436F-8D11-0B4DE2C07402")
    static let tx=CBUUID(string:"6F6C6F56-6962-436F-8D11-0B4DE2C07403")
    static func frames(_ data:Data,id:UInt16,capacity:Int)->[Data] {
        guard (1...1537).contains(data.count),capacity>6 else { return [] }
        return stride(from:0,to:data.count,by:capacity-6).map { offset in
            let size=data.count
            var frame=Data([UInt8(truncatingIfNeeded:id),UInt8(id>>8),UInt8(truncatingIfNeeded:offset),UInt8(offset>>8),UInt8(truncatingIfNeeded:size),UInt8(size>>8)])
            frame.append(data.subdata(in:offset..<min(offset+capacity-6,size)));return frame
        }
    }
}
struct BLEAssembly {
    private var bytes=Data(),id=0,size=0,started=Date.distantPast
    mutating func clear() { bytes.removeAll(keepingCapacity:false);size=0 }
    mutating func append(_ frame:Data,now:Date=Date())->Data? {
        let b=Array(frame)
        guard b.count>6 else { clear();return nil }
        let number=Int(b[0])|Int(b[1])<<8,offset=Int(b[2])|Int(b[3])<<8,total=Int(b[4])|Int(b[5])<<8
        guard (1...1537).contains(total),offset<=total,b.count-6<=total-offset else { clear();return nil }
        if offset==0 { clear();id=number;size=total;started=now }
        guard size==total,id==number,bytes.count==offset,(0...2).contains(now.timeIntervalSince(started)) else { clear();return nil }
        bytes.append(contentsOf:b.dropFirst(6))
        guard bytes.count==size else { return nil }
        let result=bytes;clear();return result
    }
}

struct BLEDiscoveryPolicy {
    private(set) var deviceEnabled=true,parked=false
    var permitsConnection:Bool { deviceEnabled && !parked }
    mutating func reportDevice(enabled:Bool) { deviceEnabled=enabled }
    mutating func retry() { deviceEnabled=true;parked=false }
    mutating func park() { parked=true }
}

/* All methods/delegates run on Engine's serial work queue. Only one peripheral;
 * the device itself arbitrates the computer owner across both transports. */
final class BLETransport:NSObject,CBCentralManagerDelegate,CBPeripheralDelegate {
    private let log=Logger(subsystem:"io.github.vibecompanion.mac",category:"BLE")
    private var manager:CBCentralManager!
    private var peripheral:CBPeripheral?,writeCharacteristic:CBCharacteristic?,notifyCharacteristic:CBCharacteristic?
    private struct Write { let data:Data,audio:Bool }
    private var assembly=BLEAssembly(),outgoing:[Write]=[],writing=false,message:UInt16=0
    private var scanUntil=Date.distantPast,nextScan=Date.distantPast,retryAt=Date.distantPast
    private var failures=0,stopped=false,policy=BLEDiscoveryPolicy(),blocked:[UUID:Date]=[:]
    private var known:CBPeripheral?
    private var connectingUntil=Date.distantPast
    var expectedDevice="",identifiedDevice=""
    private(set) var ready=false
    var onReady:((String)->Void)?,onPacket:((Data)->Void)?,onDisconnect:(()->Void)?
    var onStatus:((BluetoothPhase,String)->Void)?
    init(queue:DispatchQueue) { super.init();manager=CBCentralManager(delegate:self,queue:queue,options:[CBCentralManagerOptionShowPowerAlertKey:false]) }
    func centralManagerDidUpdateState(_ central:CBCentralManager) {
        log.notice("Central state=\(central.state.rawValue)")
        switch central.state {
        case .poweredOn:
            if policy.permitsConnection { onStatus?(.available,"蓝牙可用");retry() }
            else { onStatus?(policy.deviceEnabled ? .parked : .deviceDisabled,policy.deviceEnabled ? "蓝牙待机，切换时重新连接" : "设备蓝牙已关闭") }
        case .unauthorized: onStatus?(.unauthorized,"请在系统设置中允许 Vibe 伴侣使用蓝牙")
        case .poweredOff: onStatus?(.computerDisabled,"电脑蓝牙已关闭")
        case .unsupported: onStatus?(.unsupported,"此电脑不支持蓝牙")
        default: onStatus?(.unavailable,"蓝牙暂不可用")
        }
        if central.state != .poweredOn { invalidate() }
    }
    func setDeviceEnabled(_ enabled:Bool) {
        guard enabled != policy.deviceEnabled else { return }
        policy.reportDevice(enabled:enabled)
        if enabled {
            if policy.permitsConnection { retry() }
        } else {
            manager.stopScan();onStatus?(.deviceDisabled,"设备蓝牙已关闭")
            if let item=peripheral { manager.cancelPeripheralConnection(item) }
        }
    }
    func retry() {
        // A manual refresh can discover a device re-enabled locally while its
        // Wi-Fi is unavailable. Automatic status updates still stop retries.
        policy.retry();nextScan = .distantPast;retryAt = .distantPast
        if known==nil,!expectedDevice.isEmpty,
           let saved=UserDefaults.standard.string(forKey:"ble.peripheral."+expectedDevice),let id=UUID(uuidString:saved) {
            known=manager.retrievePeripherals(withIdentifiers:[id]).first
        }
        if peripheral==nil,let known=known,manager.state == .poweredOn {
            connect(known)
        } else { tick() }
    }
    func tick() {
        guard !stopped,policy.permitsConnection,manager.state == .poweredOn else { return }
        let now=Date()
        if let item=peripheral,!ready,now>=connectingUntil { fail(item);return }
        if manager.isScanning,now>=scanUntil { manager.stopScan();nextScan=now.addingTimeInterval(20) }
        if peripheral==nil,!manager.isScanning,now>=nextScan,now>=retryAt {
            manager.scanForPeripherals(withServices:[BLEWire.service],options:[CBCentralManagerScanOptionAllowDuplicatesKey:false]);scanUntil=now.addingTimeInterval(10)
        }
    }
    func centralManager(_ central:CBCentralManager,didDiscover item:CBPeripheral,advertisementData:[String:Any],rssi:NSNumber) {
        guard !stopped,policy.permitsConnection,peripheral==nil,(blocked[item.identifier] ?? .distantPast)<Date() else { return }
        log.notice("Service discovered");central.stopScan();connect(item)
    }
    private func connect(_ item:CBPeripheral) {
        peripheral=item;item.delegate=self;connectingUntil=Date().addingTimeInterval(60)
        onStatus?(.connecting,"正在连接蓝牙…");manager.connect(item,options:nil)
    }
    func centralManager(_ central:CBCentralManager,didConnect item:CBPeripheral) {
        guard !stopped,policy.permitsConnection,item===peripheral else { central.cancelPeripheralConnection(item);return }
        log.notice("Connected, discovering GATT");item.discoverServices([BLEWire.service])
    }
    func centralManager(_ central:CBCentralManager,didFailToConnect item:CBPeripheral,error:Error?) { disconnected(item,error:error) }
    func centralManager(_ central:CBCentralManager,didDisconnectPeripheral item:CBPeripheral,error:Error?) { disconnected(item,error:error) }
    private func disconnected(_ item:CBPeripheral,error:Error?) {
        guard item===peripheral else { return }
        failures=min(failures+1,5);retryAt=Date().addingTimeInterval(min(30,pow(2,Double(failures))))
        log.notice("Disconnected error=\((error as NSError?)?.code ?? 0)")
        invalidate()
        if !policy.deviceEnabled { onStatus?(.deviceDisabled,"设备蓝牙已关闭") }
        else if let error=error as? CBError,error.code == .peerRemovedPairingInformation {
            policy.park();known=nil
            onStatus?(.failed,"蓝牙配对已失效，请在系统蓝牙设置中忽略 Vibe 设备，再点刷新")
        } else if policy.parked { onStatus?(.parked,"蓝牙待机，切换时重新连接") }
        else if let error=error { onStatus?(.failed,"蓝牙连接已断开：\(error.localizedDescription)") }
        else { onStatus?(.available,"蓝牙未连接") }
    }
    private func invalidate() {
        let wasReady=ready;ready=false;identifiedDevice="";peripheral=nil;writeCharacteristic=nil;notifyCharacteristic=nil
        outgoing.removeAll();writing=false;assembly.clear();if wasReady { onDisconnect?() }
    }
    func peripheral(_ item:CBPeripheral,didDiscoverServices error:Error?) {
        log.notice("Services response error=\((error as NSError?)?.code ?? 0)")
        guard item===peripheral,error==nil,let service=item.services?.first(where:{$0.uuid==BLEWire.service}) else { fail(item);return }
        item.discoverCharacteristics([BLEWire.info,BLEWire.rx,BLEWire.tx],for:service)
    }
    func peripheral(_ item:CBPeripheral,didDiscoverCharacteristicsFor service:CBService,error:Error?) {
        log.notice("Characteristics response error=\((error as NSError?)?.code ?? 0)")
        guard item===peripheral,error==nil,let info=service.characteristics?.first(where:{$0.uuid==BLEWire.info}),
              let rx=service.characteristics?.first(where:{$0.uuid==BLEWire.rx}),let tx=service.characteristics?.first(where:{$0.uuid==BLEWire.tx}) else { fail(item);return }
        writeCharacteristic=rx;notifyCharacteristic=tx;item.readValue(for:info)
    }
    func peripheral(_ item:CBPeripheral,didUpdateValueFor characteristic:CBCharacteristic,error:Error?) {
        guard item===peripheral,error==nil,let data=characteristic.value else { return }
        if characteristic.uuid==BLEWire.info {
            guard let identity=String(data:data,encoding:.utf8),identity.count==12,identity.allSatisfy({"0123456789abcdef".contains($0)}),
                  expectedDevice.isEmpty || expectedDevice==identity,let tx=notifyCharacteristic else {
                blocked[item.identifier]=Date().addingTimeInterval(60);fail(item);return
            }
            identifiedDevice=identity;item.setNotifyValue(true,for:tx)
            log.notice("Device identity verified")
            known=item
            UserDefaults.standard.set(item.identifier.uuidString,forKey:"ble.peripheral."+identity)
        } else if characteristic.uuid==BLEWire.tx,ready,let packet=assembly.append(data) { onPacket?(packet) }
    }
    func peripheral(_ item:CBPeripheral,didUpdateNotificationStateFor characteristic:CBCharacteristic,error:Error?) {
        guard item===peripheral,error==nil,characteristic.isNotifying,!identifiedDevice.isEmpty else { fail(item);return }
        ready=true;failures=0;onStatus?(.connected,"蓝牙已连接");onReady?(identifiedDevice)
        log.notice("Notifications ready")
    }
    func send(_ packet:Data,audio:Bool=false)->Bool {
        guard ready,let item=peripheral else { return false }
        let frames=BLEWire.frames(packet,id:message,capacity:min(244,item.maximumWriteValueLength(for:audio ? .withoutResponse:.withResponse)))
        guard !frames.isEmpty,outgoing.count+frames.count<=64 else { return false }
        message &+= 1;outgoing.append(contentsOf:frames.map { Write(data:$0,audio:audio) });writeNext();return true
    }
    private func writeNext() {
        guard !writing,!outgoing.isEmpty,let item=peripheral,let rx=writeCharacteristic else { return }
        while !outgoing.isEmpty {
            if outgoing[0].audio {
                guard item.canSendWriteWithoutResponse else { return }
                item.writeValue(outgoing.removeFirst().data,for:rx,type:.withoutResponse)
            } else {
                writing=true;item.writeValue(outgoing.removeFirst().data,for:rx,type:.withResponse);return
            }
        }
    }
    func peripheralIsReady(toSendWriteWithoutResponse item:CBPeripheral) { if item===peripheral { writeNext() } }
    func discardRadio() { outgoing.removeAll { $0.audio } }
    func peripheral(_ item:CBPeripheral,didWriteValueFor characteristic:CBCharacteristic,error:Error?) {
        guard item===peripheral else { return }
        writing=false
        if error != nil { onStatus?(.failed,"蓝牙写入失败，请确认系统蓝牙配对授权");fail(item) } else { writeNext() }
    }
    private func fail(_ item:CBPeripheral) { if item===peripheral { manager.cancelPeripheralConnection(item) } }
    func park() {
        // Paired inactive computers release the device's temporary BLE slot.
        // Explicit takeover reconnects the remembered peripheral on demand.
        policy.park();onStatus?(.parked,"蓝牙待机，切换时重新连接");manager.stopScan();if let item=peripheral { manager.cancelPeripheralConnection(item) }
    }
    func stop() { stopped=true;manager.stopScan();if let item=peripheral { manager.cancelPeripheralConnection(item) };invalidate() }
}
