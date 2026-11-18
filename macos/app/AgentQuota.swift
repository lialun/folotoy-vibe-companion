import Foundation
import Combine
import CoreFoundation
import os

struct AgentQuotaMetric:Codable,Equatable {
    var label:String
    var kind:Int=1
    var remaining:Double=0
    var minutes:Int=0
    var resetsAt:TimeInterval?
    var amount:String="",currency:String=""
    var title:String {
        if minutes==43200 { return "每月" }
        if minutes==10080 { return "每周" }
        if minutes==1440 { return "每日" }
        if minutes>0 && minutes<1440 && minutes%60==0 { return "\(minutes/60) 小时" }
        return label
    }
    var value:String {
        if kind==2{return currency+" "+amount}
        if remaining>0 && remaining<0.1{return "剩余 <0.1%"}
        return "剩余 "+(remaining.rounded()==remaining ? String(format:"%.0f%%",remaining):String(format:"%.1f%%",remaining))
    }
    func valid(at now:Date)->Bool {
        guard AgentQuotaSample.ascii(label,24),kind==1 || kind==2 else { return false }
        if let reset=resetsAt { guard reset.isFinite,reset>now.timeIntervalSince1970,reset-now.timeIntervalSince1970<=366*86400 else{return false} }
        if kind==1 { return remaining.isFinite && (0...100).contains(remaining) && (0...527040).contains(minutes) }
        return amount.range(of:#"^[0-9]{1,12}(\.[0-9]{1,2})?$"#,options:.regularExpression) != nil && currency.range(of:#"^[A-Z]{3}$"#,options:.regularExpression) != nil
    }
}
struct AgentQuotaSample:Codable,Equatable,Identifiable {
    static let interval:TimeInterval=300, lifetime:TimeInterval=930
    var id=UUID().uuidString
    let agent:AgentID,owner:String
    var captured:Date
    var items:[AgentQuotaMetric]
    var name:String?
    static func ascii(_ value:String,_ max:Int)->Bool { !value.isEmpty && value.utf8.count<=max && value.utf8.allSatisfy{(32...126).contains($0)} }
    static func number(_ value:Any?)->Double? {
        guard let n=value as? NSNumber,CFGetTypeID(n) != CFBooleanGetTypeID(),n.doubleValue.isFinite else {return nil};return n.doubleValue
    }
    var deviceName:String { if let name=name {return name};switch agent {case .claude:return "Claude Code";case .dsh:return "DSH";case .gemini:return "Gemini CLI";case .qwen:return "Qwen Code";default:return agent.title} }
    func live(at now:Date)->[AgentQuotaMetric] {
        guard now>=captured,now.timeIntervalSince(captured)<Self.lifetime else{return []}
        return items.filter{$0.valid(at:now)}
    }
    func valid(at now:Date)->Bool {
        Self.ascii(id,64) && Self.ascii(owner,64) && captured.timeIntervalSince1970.isFinite &&
        captured.timeIntervalSince(now)<=5 && now.timeIntervalSince(captured)<Self.lifetime &&
        Self.ascii(deviceName,32) && !items.isEmpty && items.count<=4 && items.allSatisfy{$0.valid(at:captured)}
    }
    func wire(at now:Date)->[String:Any]? {
        let rows=live(at:now);guard !rows.isEmpty else{return nil}
        let age=max(0,Int(now.timeIntervalSince(captured)*1000)),ttl=Int(Self.lifetime*1000)-age
        guard ttl>0 else{return nil}
        return ["schema":1,"agent":agent.rawValue,"name":deviceName,"sample":id,"owner":owner,
                "stamp":Int(captured.timeIntervalSince1970*1000),"age_ms":age,"ttl_ms":ttl,
                "items":rows.map { row -> [String:Any] in
                    var r:[String:Any]=["kind":row.kind,"label":row.label,"reset_ms":row.resetsAt.map{max(1,Int(($0-now.timeIntervalSince1970)*1000))} ?? 0]
                    if row.kind==1 { r["remaining"]=max(row.remaining>0 ? 1:0,Int((row.remaining*100).rounded()));r["minutes"]=row.minutes }
                    else {r["amount"]=row.amount;r["currency"]=row.currency}
                    return r
                }]
    }
    static func codex(_ result:[String:Any],owner:String,at now:Date=Date())->AgentQuotaSample? {
        let buckets=result["rateLimitsByLimitId"] as? [String:Any]
        guard let bucket=(buckets?["codex"] as? [String:Any]) ?? (result["rateLimits"] as? [String:Any]) else{return nil}
        var rows:[AgentQuotaMetric]=[]
        for key in ["primary","secondary"] {
            guard let w=bucket[key] as? [String:Any],let used=number(w["usedPercent"]),(0...100).contains(used),
                  let duration=number(w["windowDurationMins"]),duration.rounded()==duration,(1...527040).contains(duration),
                  let reset=number(w["resetsAt"]),reset>now.timeIntervalSince1970 else{continue}
            let label=duration==10080 ? "Weekly" : duration==1440 ? "Daily" : duration.truncatingRemainder(dividingBy:60)==0 ? "\(Int(duration)/60)h" : "\(Int(duration))m"
            let metric=AgentQuotaMetric(label:label,remaining:100-used,minutes:Int(duration),resetsAt:reset)
            if metric.valid(at:now) {rows.append(metric)}
        }
        guard !rows.isEmpty else{return nil}
        return AgentQuotaSample(agent:.codex,owner:owner,captured:now,items:rows)
    }
    static func claude(_ input:[String:Any],at now:Date=Date())->AgentQuotaSample? {
        guard let limits=input["rate_limits"] as? [String:Any],let session=input["session_id"] as? String,!session.isEmpty else{return nil}
        var rows:[AgentQuotaMetric]=[]
        for (key,minutes,label) in [("five_hour",300,"5h"),("seven_day",10080,"Weekly")] {
            guard let w=limits[key] as? [String:Any],let used=number(w["used_percentage"]),(0...100).contains(used),
                  let reset=number(w["resets_at"]),reset>now.timeIntervalSince1970 else{continue}
            let row=AgentQuotaMetric(label:label,remaining:100-used,minutes:minutes,resetsAt:reset)
            if row.valid(at:now){rows.append(row)}
        }
        guard !rows.isEmpty,let cost=input["cost"] as? [String:Any],let duration=number(cost["total_api_duration_ms"]),duration>0 else{return nil}
        // UI redraws reuse these counters: they are not fresh API observations.
        let provenance:[String:Any]=["session":session,"duration":duration,"limits":limits,
            "tokens":(input["context_window"] as? [String:Any])?["total_input_tokens"] ?? 0]
        guard let bytes=try? JSONSerialization.data(withJSONObject:provenance,options:.sortedKeys) else{return nil}
        let scope=ProcessInfo.processInfo.environment["CLAUDE_CONFIG_DIR"] ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".claude").path
        return AgentQuotaSample(id:AgentEvent.digest(bytes.base64EncodedString()),agent:.claude,
            owner:AgentEvent.digest(scope+"|"+session),captured:now,items:rows)
    }
}

enum AgentQuotaTiming {
    static func due(now:Date,lastActivity:Date?,next:Date,busy:Bool)->Bool {
        guard let activity=lastActivity else{return false}
        return !busy && now>=next && now>=activity && now.timeIntervalSince(activity)<600
    }
    static func retry(_ failures:Int)->TimeInterval {min(1200,300*pow(2,Double(max(1,min(3,failures)))))}
}

struct AgentQuotaActivity:Codable {
    let agent:AgentID,session:String,active:Bool
    var created=Date().timeIntervalSince1970
}

// Main-queue owner. No login, Keychain prompt, browser access or paid model calls.
final class AgentQuotaCenter {
    private let log=Logger(subsystem:"io.github.vibecompanion.mac",category:"agent-quota")
    private var timer:DispatchSourceTimer?
    private var installed:Set<AgentID>=[]
    private var lastActivity:[AgentID:Date]=[:]
    private var next:[AgentID:Date]=[:],failures:[AgentID:Int]=[:],busy:Set<AgentID>=[]
    private var pendingClaude:AgentQuotaSample?
    private(set) var samples:[AgentID:AgentQuotaSample]=[:]
    private var codexExecutable:URL?
    private var stopped=false,codexEndedAt=Date.distantPast,scanning=false,nextScan=Date.distantPast
    var onChange:(([AgentQuotaSample])->Void)?
    func configure(_ rows:[AgentIntegration]) {
        installed=Set(rows.filter{$0.installed}.map{$0.agent});codexExecutable=rows.first{$0.agent == .codex}?.executable
        samples=samples.filter{$0.key.isCustom || installed.contains($0.key)};if !installed.contains(.claude){pendingClaude=nil};publish()
        guard timer==nil else{return}
        let timer=DispatchSource.makeTimerSource(queue:.main);timer.schedule(deadline:.now(),repeating:10)
        timer.setEventHandler{[weak self] in self?.tick()};self.timer=timer;timer.resume()
    }
    func activity(_ activity:AgentQuotaActivity,now:Date=Date()) {
        guard installed.contains(activity.agent),abs(now.timeIntervalSince1970-activity.created)<15,activity.session.count==64 else{return}
        if activity.active {lastActivity[activity.agent]=now}
        else {lastActivity.removeValue(forKey:activity.agent);if activity.agent == .codex{codexEndedAt=now}}
        tick(now:now)
    }
    func accept(_ incoming:AgentQuotaSample,now:Date=Date()) {
        guard (incoming.agent.isCustom || installed.contains(incoming.agent)),incoming.valid(at:now) else{return}
        guard samples[incoming.agent]?.id != incoming.id else{return}
        if let current=samples[incoming.agent],current.captured>incoming.captured{return}
        if incoming.agent == .claude {
            lastActivity[.claude]=now
            if let current=samples[.claude],current.owner==incoming.owner,now<(next[.claude] ?? .distantPast) {
                if pendingClaude == nil || pendingClaude!.captured<=incoming.captured {pendingClaude=incoming}
                return
            }
            pendingClaude=nil;next[.claude]=now.addingTimeInterval(AgentQuotaSample.interval)
        }
        samples[incoming.agent]=incoming;if !incoming.agent.isCustom && incoming.agent != .codex{lastActivity[incoming.agent]=now};publish(now:now)
    }
    private func publish(now:Date=Date()) {
        samples=samples.filter{!$0.value.live(at:now).isEmpty}
        // Keep the seven most recent sources; a callback cannot grow unbounded state.
        if samples.count>7 {samples=Dictionary(uniqueKeysWithValues:samples.values.sorted{$0.captured>$1.captured}.prefix(7).map{($0.agent,$0)})}
        onChange?(samples.values.sorted{$0.agent.rawValue<$1.agent.rawValue})
    }
    func tick(now:Date=Date()) {
        guard !stopped else{return}
        let live=samples.mapValues { sample -> AgentQuotaSample in var value=sample;value.items=sample.live(at:now);return value }
        if live != samples {samples=live;publish(now:now)}
        if let sample=pendingClaude,now>=(next[.claude] ?? .distantPast) {
            pendingClaude=nil;accept(sample,now:now)
        }
        // Bounded metadata reads run off the UI queue and at most twice a minute.
        if installed.contains(.codex),!scanning,now>=nextScan {
            scanning=true;nextScan=now.addingTimeInterval(30)
            DispatchQueue.global(qos:.utility).async { [weak self] in
                let date=Self.codexActivity(now:now)
                DispatchQueue.main.async {
                    guard let self=self else{return};self.scanning=false
                    if self.installed.contains(.codex),let date=date,now.timeIntervalSince(date)<600,date>self.codexEndedAt.addingTimeInterval(3) {
                        self.lastActivity[.codex]=max(self.lastActivity[.codex] ?? .distantPast,date)
                    }
                    self.tick()
                }
            }
        }
        guard installed.contains(.codex),let executable=codexExecutable,
              AgentQuotaTiming.due(now:now,lastActivity:lastActivity[.codex],next:next[.codex] ?? .distantPast,busy:busy.contains(.codex)) else{return}
        busy.insert(.codex);next[.codex]=now.addingTimeInterval(AgentQuotaSample.interval)
        DispatchQueue.global(qos:.utility).async { [weak self] in
            let result=Result{try CodexQuotaProbe.read(executable:executable)}
            DispatchQueue.main.async {
                guard let self=self else{return};self.busy.remove(.codex)
                guard !self.stopped,self.installed.contains(.codex) else{return}
                switch result {
                case .success(let sample):self.failures[.codex]=0;self.accept(sample)
                case .failure:
                    let failure=min(3,(self.failures[.codex] ?? 0)+1);self.failures[.codex]=failure
                    self.next[.codex]=Date().addingTimeInterval(AgentQuotaTiming.retry(failure))
                    self.log.notice("Codex quota unavailable; background retry deferred")
                }
            }
        }
    }
    static func codexActivity(now:Date=Date())->Date? {
        let home=AgentInstaller.home("CODEX_HOME",".codex").appendingPathComponent("sessions")
        let format=DateFormatter();format.timeZone=TimeZone(secondsFromGMT:0);format.dateFormat="yyyy/MM/dd"
        var latest:Date?
        for offset in [0.0,-86400.0] {
            let directory=home.appendingPathComponent(format.string(from:now.addingTimeInterval(offset)))
            let files=(try? FileManager.default.contentsOfDirectory(at:directory,includingPropertiesForKeys:[.contentModificationDateKey,.isRegularFileKey],options:.skipsHiddenFiles)) ?? []
            for file in files.filter({$0.pathExtension=="jsonl"}).sorted(by:{$0.lastPathComponent>$1.lastPathComponent}).prefix(64) {
                guard let v=try? file.resourceValues(forKeys:[.contentModificationDateKey,.isRegularFileKey]),v.isRegularFile==true,let date=v.contentModificationDate,date<=now.addingTimeInterval(5) else{continue}
                latest=max(latest ?? .distantPast,date)
            }
        }
        return latest
    }
    deinit {timer?.cancel()}
}
