import Foundation

struct PowerLock:Identifiable {
    let name:String,type:String,active:Int,heldMS:Double
    var id:String { name+"|"+type }
}
struct PowerLockGroup:Identifiable {
    let name:String,type:String,locks:[PowerLock]
    var id:String { name+"|"+type }
    var activeCount:Int { locks.filter{$0.active>0}.count }
    var title:String {
        switch name {
        case "i2s_driver":return "音频 I²S"
        case "vibe_screen":return "屏幕"
        case "wifi":return "Wi-Fi"
        case "usb_serial_jtag":return "USB 调试"
        case "spi_master":return "屏幕 SPI"
        case "I2C_0":return "I²C 总线"
        case "rtos0":return "系统调度"
        case "aes_cpu","aes_sleep":return "加密引擎"
        default:return name
        }
    }
    var constraint:String {
        switch type {
        case "NO_LIGHT_SLEEP":return "禁止浅睡"
        case "APB_FREQ_MAX":return "APB 频率锁"
        default:return "CPU 频率锁"
        }
    }
    var status:String { activeCount==0 ? "已释放" : locks.count==1 ? "持有中" : "\(activeCount)/\(locks.count) 持有" }
    var heldText:String {
        let values=locks.map{PowerDiagnostics.duration($0.heldMS)}
        if Set(values).count==1 { return (values.first ?? "—")+(values.count>1 ? " ×\(values.count)" : "") }
        return values.joined(separator:" / ")
    }
}

struct HeapSnapshot {
    let free:Double?,used:Double?,largest:Double?,minimum:Double?
    init?(_ body:[String:Any]?) {
        guard let body=body else { return nil }
        func bytes(_ key:String)->Double? {
            guard let value=(body[key] as? NSNumber)?.doubleValue,
                  value.isFinite,value>=0,value<=16*1024*1024 else { return nil }
            return value
        }
        free=bytes("free");used=bytes("used");largest=bytes("largest");minimum=bytes("min_free")
        if free==nil && used==nil && largest==nil && minimum==nil { return nil }
    }
    var usedRatio:Double? {
        guard let free=free,let used=used,free+used>0 else { return nil }
        return used/(free+used)
    }
}

// Sleep counters cover the current reset window and include entry/exit
// overhead. Lock hold times and per-region heap lows are boot-cumulative;
// active locks and heap usage are read-time snapshots, not current measurements.
struct PowerSnapshot {
    let received:Date
    let elapsedMS:Double?,blankMS:Double?,blankSleepMS:Double?,sleepMS:Double?,longestMS:Double?
    let sleeps:Int?,rejects:Int?,battery:Int?,millivolts:Int?,usb:Bool?,keyWake:Bool?
    let locks:[PowerLockGroup],rawLocks:String,locksReported:Bool,locksTruncated:Bool,unparsedLocks:Bool
    let heap:HeapSnapshot?
    init(_ body:[String:Any],received:Date=Date()) {
        self.received=received
        func number(_ key:String)->Double? {
            guard let n=(body[key] as? NSNumber)?.doubleValue,n.isFinite,n>=0,n<=1e15 else { return nil }
            return n
        }
        func integer(_ key:String)->Int? { number(key).map{Int($0)} }
        elapsedMS=number("elapsed_ms");blankMS=number("blank_ms");blankSleepMS=number("blank_sleep_ms")
        sleepMS=number("sleep_ms");longestMS=number("longest_ms");sleeps=integer("sleeps");rejects=integer("rejects")
        battery=integer("battery").flatMap{$0<=100 ? $0 : nil};millivolts=integer("millivolts").flatMap{$0>0 ? $0 : nil}
        usb=body["usb_host"] as? Bool;keyWake=body["key_wake"] as? Bool
        heap=HeapSnapshot(body["heap"] as? [String:Any])
        rawLocks=String((body["locks"] as? String ?? "").prefix(8192))
        locksReported=body["locks"] is String
        locksTruncated=body["locks_truncated"] as? Bool==true
        var parsed:[PowerLock]=[],unparsed=false
        for line in rawLocks.split(separator:"\n") {
            let fields=line.split(whereSeparator:{$0.isWhitespace}).map(String.init)
            guard fields.count==4,["NO_LIGHT_SLEEP","APB_FREQ_MAX","CPU_FREQ_MAX"].contains(fields[1]),
                  fields[2].hasPrefix("active="),fields[3].hasPrefix("held_ms="),
                  let active=Int(fields[2].dropFirst(7)),active>=0,active<=1_000_000,
                  let held=Double(fields[3].dropFirst(8)),held.isFinite,held>=0,held<=1e15 else { unparsed=true;continue }
            parsed.append(PowerLock(name:fields[0],type:fields[1],active:active,heldMS:held))
        }
        unparsedLocks=unparsed
        locks=Dictionary(grouping:parsed,by:{$0.id}).values.map{PowerLockGroup(name:$0[0].name,type:$0[0].type,locks:$0)}.sorted {
            let left=$0.activeCount>0 ? ($0.type=="NO_LIGHT_SLEEP" ? 0 : 1) : 2
            let right=$1.activeCount>0 ? ($1.type=="NO_LIGHT_SLEEP" ? 0 : 1) : 2
            return left==right ? $0.id<$1.id : left<right
        }
    }
    var blankRatio:Double? {
        guard let blank=blankMS,blank>0,let sleep=blankSleepMS,sleep<=blank else { return nil }
        return sleep/blank
    }
    var blankAwakeMS:Double? {
        guard let blank=blankMS,let sleep=blankSleepMS,sleep<=blank else { return nil }
        return blank-sleep
    }
    var screenOnMS:Double? {
        guard let elapsed=elapsedMS,let blank=blankMS,blank<=elapsed else { return nil }
        return elapsed-blank
    }
    var averageMS:Double? {
        guard let count=sleeps,count>0,let total=sleepMS else { return nil }
        return total/Double(count)
    }
    var ratioText:String { blankRatio.map{String(format:"%.1f%%",$0*100)} ?? "—" }
    var batteryText:String { [battery.map{"\($0)%"} ?? "—",millivolts.map{"\($0) mV"} ?? "—"].joined(separator:" · ") }
    var lockReportStatus:String {
        guard locksReported else { return "未提供" }
        if locksTruncated { return "已截断" }
        return unparsedLocks ? "含未识别记录" : "完整"
    }
    var textReport:String {
        func number(_ value:Double?)->String { value.map{String(format:"%.0f",$0)} ?? "null" }
        func integer(_ value:Int?)->String { value.map(String.init) ?? "null" }
        func boolean(_ value:Bool?)->String { value.map{$0 ? "true" : "false"} ?? "null" }
        let date=ISO8601DateFormatter().string(from:received)
        return ["Vibe 设备状态 \(date)",
            "elapsed_ms=\(number(elapsedMS)) blank_ms=\(number(blankMS)) blank_sleep_ms=\(number(blankSleepMS)) sleep_ms=\(number(sleepMS))",
            "sleeps=\(integer(sleeps)) rejects=\(integer(rejects)) longest_ms=\(number(longestMS))",
            "battery_pct=\(integer(battery)) battery_mv=\(integer(millivolts)) usb_host=\(boolean(usb)) key_wake=\(boolean(keyWake))",
            "heap_8bit_bytes: used=\(number(heap?.used)) free=\(number(heap?.free)) largest=\(number(heap?.largest)) min_free=\(number(heap?.minimum))",
            "locks_reported=\(locksReported) locks_truncated=\(locksTruncated) unparsed_locks=\(unparsedLocks)",
            rawLocks.trimmingCharacters(in:.whitespacesAndNewlines)].filter{!$0.isEmpty}.joined(separator:"\n")
    }
}

enum PowerDiagnostics {
    static func bytes(_ value:Double?)->String {
        guard let value=value,value.isFinite,value>=0,value<=16*1024*1024 else { return "—" }
        return String(format:"%.1f KiB",value/1024)
    }
    static func duration(_ ms:Double?)->String {
        guard let ms=ms,ms.isFinite,ms>=0,ms<=1e15 else { return "—" }
        if ms<1000 { return milliseconds(ms) }
        if ms<60000 { return String(format:"%.1f 秒",ms/1000) }
        let seconds=Int(ms/1000)
        if seconds<3600 { return "\(seconds/60) 分 \(seconds%60) 秒" }
        return "\(seconds/3600) 小时 \(seconds%3600/60) 分"
    }
    static func milliseconds(_ ms:Double?)->String {
        guard let ms=ms,ms.isFinite,ms>=0,ms<=1e15 else { return "—" }
        return String(format:"%.1f 毫秒",ms)
    }
    static func format(_ body:[String:Any])->String { PowerSnapshot(body).textReport }
}
