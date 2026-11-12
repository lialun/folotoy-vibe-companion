import Foundation
import Combine
import CryptoKit
import CoreFoundation

enum DiagnosticCode:String,Codable {
    case boot="device.boot",radioMemory="device.radio.memory",radioRemote="device.radio.remote",radioTimeout="device.radio.timeout"
    case radioDecode="device.radio.decode",radioOutput="device.radio.output",audioPrepare="device.audio.prepare"
    case audioCapture="device.audio.capture",audioSleep="device.audio.sleep",inputOverflow="device.input.overflow",settingsSave="device.settings.save",unknown="device.unknown"
    case decoderMissing="radio.decoder.missing",decoderStart="radio.decoder.start",sourceHTTP="radio.source.http",sourceTLS="radio.source.tls"
    case sourceNetwork="radio.source.network",sourceDecode="radio.source.decode",sourceEnded="radio.source.ended",sourceTimeout="radio.source.timeout",creditTimeout="radio.credit.timeout"
    case voiceStart="app.voice.start",voiceTimeout="app.voice.timeout",linkLost="app.link.lost",deviceRestart="app.device.restart",requestTimeout="app.request.timeout",historyGap="device.history.gap"
    case agentDelivery="app.agent.delivery"
    var title:String {
        switch self {
        case .boot:return "设备启动"
        case .radioMemory:return "收音机缓冲内存不足"
        case .radioRemote:return "电脑停止了电台转发"
        case .radioTimeout:return "设备等待音频超时"
        case .radioDecode:return "设备音频数据解码失败"
        case .radioOutput:return "设备扬声器输出失败"
        case .audioPrepare:return "设备音频初始化失败"
        case .audioCapture:return "设备录音失败"
        case .audioSleep:return "设备音频休眠失败"
        case .inputOverflow:return "设备按键队列溢出"
        case .settingsSave:return "设备设置保存失败"
        case .unknown:return "设备报告了异常"
        case .decoderMissing:return "电台转码组件缺失"
        case .decoderStart:return "电台转码进程启动失败"
        case .sourceHTTP:return "电台服务器返回错误"
        case .sourceTLS:return "电台安全连接失败"
        case .sourceNetwork:return "电台网络连接失败"
        case .sourceDecode:return "电台音频格式无法解码"
        case .sourceEnded:return "电台音频流已结束"
        case .sourceTimeout:return "电脑等待电台音频超时"
        case .creditTimeout:return "设备音频接收确认超时"
        case .voiceStart:return "电脑语音通道启动失败"
        case .voiceTimeout:return "语音接收超时，已结束输入"
        case .linkLost:return "当前设备链路断开"
        case .deviceRestart:return "设备重新启动，已重新连接"
        case .requestTimeout:return "设备操作响应超时"
        case .historyGap:return "部分离线记录已被覆盖"
        case .agentDelivery:return "Agent 提醒未能送达设备"
        }
    }
    var level:String { self == .boot ? "info" : [.linkLost,.deviceRestart,.historyGap].contains(self) ? "warning" : "error" }
    static func device(_ n:Int)->Self {
        [1:.boot,101:.radioMemory,102:.radioRemote,103:.radioTimeout,104:.radioDecode,105:.radioOutput,
         201:.audioPrepare,202:.audioCapture,203:.audioSleep,301:.inputOverflow,401:.settingsSave][n] ?? .unknown
    }
}

/* Only typed numeric fields, fixed codes, build identity and known preset names.
 * Raw packets, process stderr, URLs and localized system errors never enter storage. */
struct DiagnosticRecord:Codable,Identifiable {
    var id:String=UUID().uuidString
    var received=Date()
    var code:DiagnosticCode
    var link:String=""
    var firmware:String="",elf:String="",station:String=""
    var numbers:[String:Int64]=[:]
    static let fields:Set<String>=["boot","sequence","uptime_ms","detail","free","largest","stream","received","limit","exit","http","operation","oldest","after","device_code"]
    var abnormalReset:Bool { code == .boot && [4,5,6,7,9,13,14,15].contains(numbers["detail"] ?? -1) }
    var level:String { abnormalReset ? "error" : code.level }
    var title:String {
        guard abnormalReset else { return code.title }
        switch numbers["detail"] {
        case 4:return "设备因崩溃重新启动"
        case 5,6,7:return "设备因看门狗复位"
        case 9,14:return "设备因供电异常复位"
        default:return "设备异常复位"
        }
    }
    var valid:Bool {
        (UUID(uuidString:id) != nil || id.count==64 && id.allSatisfy{"0123456789abcdef".contains($0)}) &&
        ["","wifi","ble"].contains(link) && firmware.utf8.count<=80 &&
        firmware.unicodeScalars.allSatisfy{CharacterSet(charactersIn:"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-").contains($0)} &&
        (elf.isEmpty || elf.count==64 && elf.allSatisfy{"0123456789abcdef".contains($0)}) &&
        (station.isEmpty || station=="自定义电台" || RadioStation.presets.contains{$0.name==station}) &&
        Set(numbers.keys).isSubset(of:Self.fields) && numbers.count<=16 && received.timeIntervalSince1970.isFinite
    }
    var details:String {
        var parts=[String]()
        if !station.isEmpty { parts.append(station) }
        if let uptime=numbers["uptime_ms"] { parts.append("开机后 \(uptime/1000) 秒") }
        if let free=numbers["free"],let largest=numbers["largest"] { parts.append("空闲 \(free/1024) KiB · 连续 \(largest/1024) KiB") }
        if let status=numbers["http"] { parts.append("HTTP \(status)") }
        return parts.joined(separator:" · ")
    }
    static func deviceRows(_ body:[String:Any],device:String,boot:Int,after:UInt32)->[DiagnosticRecord]? {
        func u32(_ value:Any?)->UInt32? {
            guard let n=value as? NSNumber,CFGetTypeID(n) != CFBooleanGetTypeID(),n.doubleValue.isFinite,
                  n.doubleValue>=0,n.doubleValue<=Double(UInt32.max),n.doubleValue.rounded()==n.doubleValue else { return nil }
            return n.uint32Value
        }
        guard let newest=u32(body["sequence"]),let oldest=u32(body["oldest"]),oldest<=newest,
              let rows=body["records"] as? [[String:Any]],rows.count<=3,
              let firmware=body["firmware"] as? String,let elf=body["elf"] as? String else { return nil }
        guard !rows.isEmpty || newest<=after else { return nil }
        var result=[DiagnosticRecord](),previous=after
        if oldest>0 && after<oldest-1 {
            var gap=DiagnosticRecord(code:.historyGap);gap.numbers=["boot":Int64(boot),"after":Int64(after),"oldest":Int64(oldest)]
            gap.id=identity(device,boot,"gap-\(oldest)");result.append(gap)
        }
        for row in rows {
            guard let seq=u32(row["sequence"]),seq>previous,seq>=oldest,seq<=newest,
                  let uptime=u32(row["uptime_ms"]),let code=u32(row["code"]),let free=u32(row["free"]),let largest=u32(row["largest"]),
                  let route=u32(row["link"]),route<=2,let detail=row["detail"] as? NSNumber,
                  CFGetTypeID(detail) != CFBooleanGetTypeID(),detail.doubleValue>=Double(Int32.min),detail.doubleValue<=Double(Int32.max),detail.doubleValue.rounded()==detail.doubleValue else { return nil }
            var record=DiagnosticRecord(code:.device(Int(code)));record.id=identity(device,boot,String(seq))
            record.link=route==1 ? "wifi" : route==2 ? "ble" : "";record.firmware=firmware;record.elf=elf
            record.numbers=["boot":Int64(boot),"sequence":Int64(seq),"uptime_ms":Int64(uptime),"detail":detail.int64Value,"free":Int64(free),"largest":Int64(largest)]
            if code==102 || code==103 || code==104 || code==105 { record.numbers["stream"]=Int64(UInt32(bitPattern:detail.int32Value)) }
            if record.code == .unknown { record.numbers["device_code"]=Int64(code) }
            guard record.valid else { return nil };result.append(record);previous=seq
        }
        return result
    }
    private static func identity(_ device:String,_ boot:Int,_ sequence:String)->String {
        SHA256.hash(data:Data("\(device)/\(boot)/\(sequence)".utf8)).map{String(format:"%02x",$0)}.joined()
    }
}

struct DiagnosticJournal {
    static let capacity=500,maxBytes=512*1024
    private(set) var records=[DiagnosticRecord]()
    mutating func append(_ rows:[DiagnosticRecord]) {
        var ids=Set(records.map(\.id))
        records.append(contentsOf:rows.filter{$0.valid && ids.insert($0.id).inserted})
        if records.count>Self.capacity { records.removeFirst(records.count-Self.capacity) }
    }
    func encoded()throws->Data { try JSONEncoder().encode(records) }
    mutating func load(_ data:Data)throws {
        guard data.count<=Self.maxBytes else { throw CocoaError(.fileReadCorruptFile) }
        let loaded=try JSONDecoder().decode([DiagnosticRecord].self,from:data)
        guard loaded.count<=Self.capacity,loaded.allSatisfy(\.valid) else { throw CocoaError(.fileReadCorruptFile) }
        records=[];append(loaded)
    }
}

final class DiagnosticCenter:ObservableObject {
    @Published private(set) var records=[DiagnosticRecord]()
    @Published private(set) var storageError=""
    @Published var visibleCount=50
    private let io=DispatchQueue(label:"vibe.diagnostics.storage",qos:.utility)
    private var journal=DiagnosticJournal(),writable=true
    private let directory:URL
    init(preview:Bool=false,directory:URL=PairingStore.directory.appendingPathComponent("Diagnostics",isDirectory:true)) {
        self.directory=directory
        if preview { return }
        io.async {
            let file=directory.appendingPathComponent("events.json")
            do { if FileManager.default.fileExists(atPath:file.path) {
                let size=(try file.resourceValues(forKeys:[.fileSizeKey])).fileSize ?? 0
                guard size<=DiagnosticJournal.maxBytes else { throw CocoaError(.fileReadCorruptFile) }
                try self.journal.load(Data(contentsOf:file))
            };self.publish() }
            catch { self.writable=false;self.publish(error:"历史记录无法读取，原文件已保留；新记录暂存于本次运行。") }
        }
    }
    func append(_ rows:[DiagnosticRecord]) {
        guard !rows.isEmpty else { return }
        io.async {
            let before=self.journal.records.count,last=self.journal.records.last?.id
            self.journal.append(rows)
            guard before != self.journal.records.count || last != self.journal.records.last?.id else { return }
            do {
                if self.writable {
                    let files=FileManager.default
                    try files.createDirectory(at:self.directory,withIntermediateDirectories:true,attributes:[.posixPermissions:0o700])
                    try files.setAttributes([.posixPermissions:0o700],ofItemAtPath:self.directory.path)
                    let data=try self.journal.encoded()
                    guard data.count<=DiagnosticJournal.maxBytes else { throw CocoaError(.fileWriteOutOfSpace) }
                    let file=self.directory.appendingPathComponent("events.json")
                    try data.write(to:file,options:.atomic)
                    try files.setAttributes([.posixPermissions:0o600],ofItemAtPath:file.path)
                }
                self.publish()
            } catch { self.publish(error:"诊断记录无法保存，请检查当前账户的文件权限和磁盘空间。") }
        }
    }
    private func publish(error:String?=nil) {
        let copy=Array(journal.records.reversed())
        DispatchQueue.main.async { self.records=copy;if let error=error { self.storageError=error } }
    }
    func report(power:PowerSnapshot?=nil)->String {
        struct Report:Encodable { let schema=1,app:String,system:String,architecture:String;let power:String?;let records:[DiagnosticRecord] }
        #if arch(arm64)
        let architecture="arm64"
        #else
        let architecture="x86_64"
        #endif
        let encoder=JSONEncoder();encoder.outputFormatting=[.prettyPrinted,.sortedKeys];encoder.dateEncodingStrategy = .iso8601
        return (try? String(data:encoder.encode(Report(app:AppMetadata.version ?? "development",system:ProcessInfo.processInfo.operatingSystemVersionString,architecture:architecture,power:power?.textReport,records:records)),encoding:.utf8)) ?? "{}"
    }
}
