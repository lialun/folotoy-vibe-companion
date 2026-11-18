import Foundation

enum AgentBridgeTests {
    static func run(_ check:(Bool,String)->Void) {
        let now=Date(timeIntervalSince1970:1790000000)
        var alert:[String:Any]=["schema":1,"source":"my-company-agent","name":"Company Agent","event":"approval","session_id":"s","event_id":"request-1"]
        let event=AgentBridge.notification(alert,now:now)!
        check(event.valid && event.agent.isCustom && event.displayTitle=="Company Agent" && event.kind == .approval,"custom callback keeps its own source and display name")
        check(event.id==AgentBridge.notification(alert,now:now.addingTimeInterval(2))?.id,"custom notification retries retain a device dedupe identity")
        alert["event_id"]="request-2";check(event.id != AgentBridge.notification(alert,now:now)?.id,"distinct requests never share notification identity")
        alert["event"]="idle";check(AgentBridge.notification(alert,now:now)==nil,"custom idle is not completion")
        alert["event"]="completed";alert["is_subagent"]=true;check(AgentBridge.notification(alert,now:now)==nil,"custom child callback filtered")
        let rows:[[String:Any]]=[ ["label":"5h","remaining":32.5,"window_minutes":300,"resets_at":now.timeIntervalSince1970+3600], ["label":"Weekly","remaining":0,"window_minutes":10080], ["label":"Monthly","remaining":99,"window_minutes":43200]]
        var body:[String:Any]=["schema":1,"source":"company-agent","name":"My Agent","sample_id":"poll-1","observed_at":now.timeIntervalSince1970,"items":rows]
        let sample=AgentBridge.quota(body,now:now)!
        check(sample.items.count==3 && sample.items[2].title=="每月" && sample.deviceName=="My Agent","generic quota carries three windows and real exhausted value")
        check(AgentBridge.quota(body,now:now.addingTimeInterval(930))==nil,"public callback cannot revive a stale observation")
        check(AgentBridge.quota(body,now:now.addingTimeInterval(100))?.captured==sample.captured,"generic replay preserves capture time")
        check(AgentBridge.quota(body,builtin:.opencode,now:now)?.agent == .opencode,"private OpenCode adapter uses its own builtin identity")
        body["items"]=[["label":"Weekly","remaining":true]];check(AgentBridge.quota(body,now:now)==nil,"boolean is never a percentage")
        body["items"]=[["label":"Balance","amount":"12.50","currency":"USD"]];check(AgentBridge.quota(body,now:now)?.items[0].kind==2,"generic bridge supports independent currency balances")
        body["name"]="Agent\nInjected";check(AgentBridge.quota(body,now:now)==nil,"unsupported source name rejected")
        let center=AgentQuotaCenter();center.configure([])
        center.accept(sample,now:now);check(center.samples[sample.agent]?.id==sample.id,"custom quotas require no builtin installation")
        center.accept(sample,now:now.addingTimeInterval(300));check(center.samples[sample.agent]?.captured==sample.captured,"center replay does not renew source lifetime")
        for i in 0..<12 {var next=sample;next=AgentQuotaSample(id:"sample-\(i)",agent:.custom("custom-\(i)"),owner:"opaque-owner",captured:now,items:next.items,name:"Agent \(i)");center.accept(next,now:now)}
        check(center.samples.count==7,"custom quota sources have a fixed memory bound")
        center.tick(now:now.addingTimeInterval(930));check(center.samples.isEmpty,"custom quota expiry clears all rows")
        check(AgentID.allCases.count==7 && (try? JSONDecoder().decode(AgentID.self,from:Data("\"codex\"".utf8))) == .codex,"builtin identity remains wire compatible")
    }
}
