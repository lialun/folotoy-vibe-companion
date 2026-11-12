import Foundation

enum DiagnosticsTests {
    static func run(_ check:(Bool,String)->Void) {
        let row:[String:Any]=["sequence":13,"uptime_ms":12000,"code":103,"detail":-1,"free":40000,"largest":20000,"link":2]
        var body:[String:Any]=["sequence":20,"oldest":13,"firmware":"0.8.3-test","elf":String(repeating:"a",count:64),"records":[row],"token":"must-not-export","url":"https://secret.invalid/private"]
        let decoded=DiagnosticRecord.deviceRows(body,device:"private-device-id",boot:7,after:0)!
        check(decoded.count==2 && decoded[0].code == .historyGap && decoded[1].code == .radioTimeout,"offline overflow is visible, with typed device failure")
        check(decoded[1].numbers["stream"]==Int64(UInt32.max) && decoded[1].link=="ble","device stream bit pattern survives signed wire detail")
        check(!decoded[1].id.contains("private-device-id"),"device identity is hashed before local persistence")
        body["records"]=[row,row];check(DiagnosticRecord.deviceRows(body,device:"a",boot:1,after:0)==nil,"reject duplicate or unordered device sequence")
        var malformed=row;malformed["sequence"]=true;body["records"]=[malformed]
        check(DiagnosticRecord.deviceRows(body,device:"a",boot:1,after:0)==nil,"reject boolean masquerading as sequence")
        body["records"]=[row];body["firmware"]="https://private.invalid/?token=secret"
        check(DiagnosticRecord.deviceRows(body,device:"a",boot:1,after:0)==nil,"reject arbitrary build strings")
        var journal=DiagnosticJournal();journal.append(decoded);journal.append(decoded)
        check(journal.records.count==2,"duplicate device batches do not duplicate saved events")
        for i in 0..<1000 { var entry=DiagnosticRecord(code:.creditTimeout);entry.id=String(format:"%064x",i);journal.append([entry]) }
        check(journal.records.count==500 && journal.records.first?.id==String(format:"%064x",500),"desktop history evicts oldest after 500 events")
        let data=try! journal.encoded();var restored=DiagnosticJournal();try! restored.load(data)
        check(restored.records.count==500 && restored.records.last?.id==String(format:"%064x",999) && data.count<DiagnosticJournal.maxBytes,"bounded history survives JSON reload")
        var invalid=DiagnosticRecord(code:.sourceHTTP);invalid.numbers=["token":1];let before=journal.records.count
        journal.append([invalid]);check(journal.records.count==before && journal.records.last?.id==String(format:"%064x",999),"reject unapproved fields before persistence")
        check((try? restored.load(Data(repeating:0,count:DiagnosticJournal.maxBytes+1)))==nil,"oversized diagnostics never load into memory")
        let secrets=String(decoding:try! JSONEncoder().encode(decoded),as:UTF8.self)
        check(!secrets.contains("must-not-export") && !secrets.contains("private.invalid") && !secrets.contains("private-device-id"),"only whitelisted fields enter reports")
        var reset=DiagnosticRecord(code:.boot);reset.numbers=["detail":4]
        check(reset.level=="error" && reset.title.contains("崩溃"),"panic reboot is an error rather than a normal power-on")
        let center=DiagnosticCenter(preview:true),power=PowerSnapshot(["elapsed_ms":1200,"heap":["free":20480]])
        let report=try! JSONSerialization.jsonObject(with:Data(center.report(power:power).utf8)) as! [String:Any]
        check((report["power"] as? String)==power.textReport && report["records"] is [Any],"one device status report contains original power counters and event records")
        let offline=try! JSONSerialization.jsonObject(with:Data(center.report().utf8)) as! [String:Any]
        check(offline["power"]==nil && offline["records"] is [Any],"offline report does not invent a runtime snapshot")
        if let i=CommandLine.arguments.firstIndex(of:"--diagnostic-vector"),CommandLine.arguments.count>i+1,
           let data=try? Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[i+1])),
           let body=try? JSONSerialization.jsonObject(with:data) as? [String:Any] {
            let records=DiagnosticRecord.deviceRows(body,device:"fixture",boot:1,after:0)
            check(records?.count==4 && records?.last?.numbers["sequence"]==15,"actual C device response parses in Swift, including overwrite marker")
        }
    }
}
