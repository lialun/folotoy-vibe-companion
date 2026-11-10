import Foundation
import Combine

import CryptoKit

enum AgentID:String,CaseIterable,Codable,Identifiable {
    case codex,claude,opencode,dsh,zcode,gemini,qwen
    var id:String { rawValue }
    var title:String { switch self { case .codex:return "Codex";case .claude:return "Claude Code";case .opencode:return "OpenCode";case .dsh:return "DeepSeek Harness";case .zcode:return "ZCode";case .gemini:return "Gemini CLI";case .qwen:return "Qwen Code" } }
    var coverage:String {
        switch self {
        case .codex:return "完成 · 审批 · 支持工具 Hook 的提问"
        case .gemini:return "完成 · 审批"
        case .zcode:return "完成 · 审批"
        default:return "完成 · 审批 · 提问"
        }
    }
}
enum AgentKind:String,Codable { case completed,approval,input,failed
    var title:String { switch self {case .completed:return "任务已完成";case .approval:return "等待审批";case .input:return "等待回答";case .failed:return "任务出错"} }
}
struct AgentEvent:Codable,Identifiable {
    var id=UUID().uuidString
    let agent:AgentID,kind:AgentKind,session:String,key:String,project:String
    var created=Date().timeIntervalSince1970
    static func digest(_ text:String)->String { SHA256.hash(data:Data(text.utf8)).map{String(format:"%02x",$0)}.joined() }
    // Never forward prompts, responses, tool input or transcripts. Keep only identities.
    static func parse(agent:AgentID,event:String="",payload:[String:Any])->AgentEvent? {
        let p=payload
        if p["parent_session_id"] as? String != nil || p["parentSessionID"] as? String != nil || p["is_subagent"] as? Bool==true { return nil }
        let name=event.isEmpty ? (p["hook_event_name"] as? String ?? p["hookEventName"] as? String ?? "") : event
        let tool=p["tool_name"] as? String ?? ""
        let notification=p["notification_type"] as? String ?? ""
        let kind:AgentKind
        switch name {
        case "Stop","AfterAgent","completed":kind = .completed
        case "PermissionRequest","approval":kind = .approval
        case "input":kind = .input
        case "failed","StopFailure":kind = .failed
        case "PreToolUse":
            let questions=["AskUserQuestion","ExitPlanMode","request_user_input","request_user_input_async","ask_user_question","exit_plan_mode"]
            guard questions.contains(tool) else { return nil };kind = .input
        case "Notification":
            if agent == .gemini,notification=="ToolPermission" { kind = .approval }
            else if agent == .claude,["elicitation_dialog","elicitation_url_dialog"].contains(notification) { kind = .input }
            else { return nil }
        default:return nil
        }
        let session=String((p["session_id"] as? String ?? p["sessionId"] as? String ?? p["session"] as? String ?? "unknown").prefix(200))
        let turn=p["turn_id"] as? String ?? p["turnId"] as? String ?? ""
        let request=p["request_id"] as? String ?? p["tool_use_id"] as? String ?? ""
        let cwd=p["cwd"] as? String ?? ""
        let project=String(URL(fileURLWithPath:cwd).lastPathComponent.filter{!$0.isNewline}.prefix(48))
        // The short dedupe window also works for Agents which do not expose turn IDs.
        let key=digest([agent.rawValue,session,kind.rawValue,turn,request].joined(separator:"|"))
        return AgentEvent(agent:agent,kind:kind,session:session,key:key,project:project)
    }
    var valid:Bool { UUID(uuidString:id) != nil && session.utf8.count<=800 && key.count==64 && project.count<=48 && created.isFinite }
    var deviceBody:[String:Any] { ["agent":agent.rawValue,"kind":kind.rawValue] }
}

// Main-queue owner. Bounded, ephemeral; no transcript polling or extra keepalive.
final class AgentCenter:ObservableObject {
    @Published var integrations:[AgentIntegration]=[]
    @Published var recent:[AgentEvent]=[]
    @Published var message=""
    @Published var busy=false
    @Published var dshPathCopied=false
    @Published var enabled=UserDefaults.standard.object(forKey:"agentAlertsEnabled") as? Bool ?? true {
        didSet { if !preview { UserDefaults.standard.set(enabled,forKey:"agentAlertsEnabled") } }
    }
    private let preview:Bool
    private var receiver:AgentReceiver?
    private var seen:[String:TimeInterval]=[:]
    private var scans=0
    var deliver:((AgentEvent)->Void)?
    init(preview:Bool=false) {
        self.preview=preview
        if preview { integrations=AgentID.allCases.map{AgentIntegration(agent:$0,executable:nil,application:nil,installed:false)};return }
        do { let r=AgentReceiver();try r.start { [weak self] in self?.accept($0) };receiver=r }
        catch { message="通知接收未启动："+error.localizedDescription }
        refresh()
    }
    func refresh() {
        guard !busy else { return };busy=true;scans+=1;let scan=scans
        DispatchQueue.global(qos:.utility).async { [self] in
            let rows=AgentInstaller.detect()
            DispatchQueue.main.async { [weak self] in guard let self=self,self.scans==scan else { return };self.integrations=rows;self.busy=false }
        }
    }
    func accept(_ event:AgentEvent,now:TimeInterval=Date().timeIntervalSince1970) {
        guard event.valid,abs(now-event.created)<15 else { return }
        seen=seen.filter{let age=now-$0.value;return age>=0 && age<3}
        guard seen[event.key]==nil else { return }
        // One interaction can emit both question and permission callbacks.
        // Keep the first immediately; do not delay or extend its one-second window.
        // Missing identities must not combine unrelated sessions.
        let group = event.session.isEmpty || event.session=="unknown" ? nil
            : "session:"+AgentEvent.digest(event.agent.rawValue+"|"+event.session)
        let suppressed=group.flatMap{seen[$0]}.map{now-$0<1} ?? false
        seen[event.key]=now // Also suppress retries of a discarded overlapping callback.
        if let group=group,!suppressed { seen[group]=now }
        if seen.count>128 {
            seen=Dictionary(uniqueKeysWithValues:seen.sorted{$0.value>$1.value}.prefix(128).map{($0.key,$0.value)})
        }
        guard !suppressed else { return }
        recent.insert(event,at:0);recent=Array(recent.prefix(12))
        guard enabled else { return }
        deliver?(event)
    }
    func change(_ row:AgentIntegration,install:Bool) {
        guard !busy else { return };busy=true;message=""
        DispatchQueue.global(qos:.utility).async { [self] in
            let result:Result<String,Error>=Result { try AgentInstaller.change(row,install:install) }
            DispatchQueue.main.async { [weak self] in
                guard let self=self else { return };self.busy=false
                switch result {case .success(let text):self.message=text;case .failure(let error):self.message=error.localizedDescription}
                self.refresh()
            }
        }
    }
    func test() {
        guard enabled else { message="请先开启设备提醒";return }
        let e=AgentEvent(agent:.codex,kind:.completed,session:"test",key:UUID().uuidString.replacingOccurrences(of:"-",with:"").padding(toLength:64,withPad:"0",startingAt:0),project:"测试通知")
        deliver?(e)
    }
}
