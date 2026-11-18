import Foundation
import Darwin
import os

// Public local bridge. The Agent keeps its own login and sends metadata only.
enum AgentBridge {
    static func identity(_ p:[String:Any],builtin:AgentID?=nil)->(AgentID,String)? {
        guard let source=p["source"] as? String,source.range(of:#"^[a-z0-9][a-z0-9._-]{0,63}$"#,options:.regularExpression) != nil,
              let name=p["name"] as? String,AgentQuotaSample.ascii(name,32) else{return nil}
        return (builtin ?? .custom(source),name)
    }
    static func uuid(_ seed:String)->String {
        let h=Array(AgentEvent.digest(seed).prefix(32));return [String(h[0..<8]),String(h[8..<12]),String(h[12..<16]),String(h[16..<20]),String(h[20..<32])].joined(separator:"-")
    }
    static func notification(_ p:[String:Any],now:Date=Date())->AgentEvent? {
        guard AgentQuotaSample.number(p["schema"])==1,let (agent,name)=identity(p),
              let kindName=p["event"] as? String,let kind=AgentKind(rawValue:kindName),
              let session=p["session_id"] as? String,!session.isEmpty,session.utf8.count<=200,
              let occurrence=p["event_id"] as? String,!occurrence.isEmpty,occurrence.utf8.count<=200,
              p["is_subagent"] as? Bool != true,p["parent_session_id"]==nil else{return nil}
        let key=AgentEvent.digest(agent.rawValue+"|"+session+"|"+kind.rawValue+"|"+occurrence)
        return AgentEvent(id:uuid(key),agent:agent,kind:kind,session:AgentEvent.digest(session),key:key,project:"",created:now.timeIntervalSince1970,name:name)
    }
    static func quota(_ p:[String:Any],builtin:AgentID?=nil,now:Date=Date())->AgentQuotaSample? {
        guard AgentQuotaSample.number(p["schema"])==1,let (agent,name)=identity(p,builtin:builtin),
              let sample=p["sample_id"] as? String,!sample.isEmpty,sample.utf8.count<=200,
              let observed=AgentQuotaSample.number(p["observed_at"]),
              let items=p["items"] as? [[String:Any]],!items.isEmpty,items.count<=4 else{return nil}
        let account=p["account"] as? String ?? "local";guard account.utf8.count<=200,p["account"]==nil || p["account"] is String else{return nil}
        var rows:[AgentQuotaMetric]=[]
        for item in items {
            guard let label=item["label"] as? String else{return nil}
            let reset=AgentQuotaSample.number(item["resets_at"])
            if item["resets_at"] != nil && reset==nil{return nil}
            if let amount=item["amount"] as? String,let currency=item["currency"] as? String {
                guard item["remaining"]==nil else{return nil}
                rows.append(AgentQuotaMetric(label:label,kind:2,resetsAt:reset,amount:amount,currency:currency))
            } else {
                guard let remaining=AgentQuotaSample.number(item["remaining"]) else{return nil}
                let minutes=AgentQuotaSample.number(item["window_minutes"]) ?? 0
                if item["window_minutes"] != nil && AgentQuotaSample.number(item["window_minutes"])==nil{return nil}
                guard minutes.rounded()==minutes,(0...527040).contains(minutes) else{return nil}
                rows.append(AgentQuotaMetric(label:label,remaining:remaining,minutes:Int(minutes),resetsAt:reset))
            }
        }
        let owner=AgentEvent.digest(agent.rawValue+"|"+account)
        let result=AgentQuotaSample(id:AgentEvent.digest(owner+"|"+sample),agent:agent,owner:owner,captured:Date(timeIntervalSince1970:observed),items:rows,name:name)
        return result.valid(at:now) ? result:nil
    }
    static func run(quota:Bool,builtin:AgentID?=nil) {
        defer{print("{}")}
        let deadline=Date().addingTimeInterval(0.8)
        var data=Data(),buffer=[UInt8](repeating:0,count:4097)
        while Date()<deadline && data.count<=4096 {
            var fd=pollfd(fd:STDIN_FILENO,events:Int16(POLLIN),revents:0)
            let ready=poll(&fd,1,25);if ready<0{return};if ready==0{continue}
            let count=Darwin.read(STDIN_FILENO,&buffer,buffer.count);if count==0{break};if count<0{return}
            data.append(contentsOf:buffer.prefix(count))
        }
        guard data.count<=4096,let p=try? JSONSerialization.jsonObject(with:data) as? [String:Any] else{return}
        let sent:Bool
        if quota,let sample=Self.quota(p,builtin:builtin),let body=try? JSONSerialization.jsonObject(with:JSONEncoder().encode(sample)) {sent=AgentReceiver.forwardMessage(["quota":body])}
        else if !quota,let event=notification(p) {sent=AgentReceiver.forward(event)}
        else{return}
        if !sent {Logger(subsystem:"io.github.vibecompanion.mac",category:"agent-bridge").debug("Local Agent callback skipped; companion unavailable")}
    }
    static var prompt:String {
        """
        请给我正在使用的 Agent 接入 Vibe 伴侣：任务提醒和额度展示。先检查 Agent 的官方 Hook／插件及额度接口，再实现最小适配器，保留现有配置。

        本机 helper：\(AgentInstaller.helper.path)
        用参数数组启动 helper，通过标准输入传入一个 JSON 对象，不拼接 shell 命令。

        提醒：参数 --notify
        {"schema":1,"source":"my-agent","name":"My Agent","event":"completed","session_id":"session-123","event_id":"turn-456"}
        event 支持 completed（主任务完成）、approval（等待审批）、input（等待回答）、failed（任务失败）。过滤子任务、暂时 idle 和正常取消。同一事件重试保持 event_id 不变。

        额度：参数 --quota
        {"schema":1,"source":"my-agent","name":"My Agent","account":"account-1","sample_id":"observation-123","observed_at":UNIX秒,"items":[{"label":"Weekly","remaining":73.5,"window_minutes":10080,"resets_at":UNIX秒}]}
        仅使用 Agent 已有登录态提供的真实额度。remaining 为剩余百分比（0–100）；items 最多4项；重置时间和窗口分钟数可省略。金额使用 amount（如 "12.50"）、currency（如 "USD"），替换 remaining。source 固定，name／label 用简短英文（最多32／24字节）。sample_id 和 observed_at 对应实际采集，重发不改变。不要把示例数值写入实际集成。

        最近使用时每5分钟采集一次，空闲10分钟停止；失败退避10／20分钟。额度15分30秒过期自动隐藏。取不到就安静跳过，不要求额外登录或复制密钥。

        只发送上述元数据，凭据留在 Agent 内。回调不阻塞任务，helper 超时1秒；未运行或离线时安静跳过。请验证事件映射、重复过滤、额度过期及原配置保留，并给出启用和卸载方法。
        """
    }
}
