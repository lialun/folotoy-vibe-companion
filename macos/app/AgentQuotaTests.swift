import Foundation

enum AgentQuotaTests {
    static func run(_ check:(Bool,String)->Void) {
        CodexQuotaProbeTests.run(check)
        let now=Date(timeIntervalSince1970:1790000000),owner="synthetic-owner"
        var schedule=QuotaPollSchedule()
        check(schedule.begin(now:now) && !schedule.begin(now:now),"one active quota poll reserves its interval")
        schedule.cancel()
        check(schedule.begin(now:now.addingTimeInterval(10)),"sleep-cancelled quota poll can retry promptly after wake")
        schedule.finish(success:true,now:now.addingTimeInterval(11));schedule.cancel()
        check(!schedule.begin(now:now.addingTimeInterval(100)) && schedule.next==now.addingTimeInterval(310),"sleep preserves a completed poll's five-minute interval")
        check(schedule.begin(now:now.addingTimeInterval(310)),"completed query becomes due on its original schedule")
        schedule.finish(success:false,now:now.addingTimeInterval(311));schedule.cancel()
        check(schedule.failures==1 && !schedule.begin(now:now.addingTimeInterval(500)),"sleep preserves genuine quota failure backoff")
        check(schedule.begin(now:now.addingTimeInterval(911)),"backoff eventually permits a query")
        schedule.cancel()
        check(schedule.failures==1 && schedule.begin(now:now.addingTimeInterval(912)),"cancelling a retry adds neither a new cooldown nor a false failure")
        schedule.finish(success:true,now:now.addingTimeInterval(913))
        check(schedule.failures==0,"successful recovery resets failure count")
        check(AgentQuotaTiming.due(now:now,lastActivity:now,next:.distantPast,busy:false),"first active observation queries promptly")
        check(!AgentQuotaTiming.due(now:now.addingTimeInterval(299),lastActivity:now,next:now.addingTimeInterval(300),busy:false),"repeated use cannot bypass five-minute query cooldown")
        check(!AgentQuotaTiming.due(now:now.addingTimeInterval(600),lastActivity:now,next:.distantPast,busy:false),"idle source stops after ten minutes")
        check(!AgentQuotaTiming.due(now:now,lastActivity:now,next:.distantPast,busy:true),"concurrent source queries coalesce")
        check(AgentQuotaTiming.retry(1)==600 && AgentQuotaTiming.retry(2)==1200 && AgentQuotaTiming.retry(20)==1200,"silent retry backs off ten then twenty minutes")
        func result(_ used:Any=53,_ reset:Double=1790100000)->[String:Any] {
            ["rateLimitsByLimitId":["codex":["primary":["usedPercent":used,"windowDurationMins":10080,"resetsAt":reset],"secondary":NSNull()]]]
        }
        let one=AgentQuotaSample.codex(result(),owner:owner,at:now)!
        check(one.items.count==1 && one.items[0].remaining==47 && one.items[0].title=="每周","quota honors a lone weekly primary, not an invented 5h window")
        check(AgentQuotaSample.codex(result(true),owner:owner,at:now)==nil,"boolean quota rejected")
        check(AgentQuotaSample.codex(result(-1),owner:owner,at:now)==nil,"negative quota rejected")
        check(AgentQuotaSample.codex(result(0,now.timeIntervalSince1970),owner:owner,at:now)==nil,"reset window cannot be reused")
        check(AgentQuotaSample.codex(result(100),owner:owner,at:now)?.items[0].remaining==0,"real zero quota retained")
        let wire=one.wire(at:now.addingTimeInterval(300))!
        check(wire["ttl_ms"] as? Int==630000 && wire["sample"] as? String==one.id,"reconnect packet retains original sample identity and remaining lifetime")
        check(one.wire(at:now.addingTimeInterval(930))==nil,"expired data omitted")
        check(AgentQuotaMetric(label:"5h",remaining:0.01).value=="剩余 <0.1%","positive fraction never appears as exhausted")
        var tiny=one;tiny.items[0].remaining=0.0001
        check((tiny.wire(at:now)?["items"] as? [[String:Any]])?.first?["remaining"] as? Int==1,"wire precision never turns a positive quota into zero")
        let claude:[String:Any]=["session_id":"synthetic-session","cost":["total_api_duration_ms":1000],
            "rate_limits":["five_hour":["used_percentage":25.5,"resets_at":now.timeIntervalSince1970+3600]]]
        let first=AgentQuotaSample.claude(claude,at:now)!,redraw=AgentQuotaSample.claude(claude,at:now.addingTimeInterval(30))!
        check(first.id==redraw.id && first.items[0].remaining==74.5,"Claude UI redraw retains a stable observation ID")
        check(AgentQuotaSample.claude(["session_id":"s"],at:now)==nil,"missing Claude quota stays unavailable")
        let center=AgentQuotaCenter();center.configure([AgentIntegration(agent:.claude,executable:nil,application:nil,installed:true)])
        center.accept(first,now:now)
        var newer=first;newer.id="new-observation";newer.captured=now.addingTimeInterval(1);newer.items[0].remaining=70
        center.accept(newer,now:newer.captured)
        check(center.samples[.claude]?.id==first.id,"frequent statusline observations coalesce within five minutes")
        center.tick(now:now.addingTimeInterval(300))
        check(center.samples[.claude]?.id==newer.id && center.samples[.claude]?.captured==newer.captured,"coalesced statusline uses its original capture time, not delivery time")
        center.suspend()
        center.tick(now:now.addingTimeInterval(2000))
        check(center.samples[.claude]?.id==newer.id,"sleep suspends quota timer work")
        center.resume(now:now.addingTimeInterval(2000))
        check(center.samples.isEmpty,"wake expires old quota observations without renewing their lifetime")
        center.suspend()
        let temp=FileManager.default.temporaryDirectory.appendingPathComponent("vibe-quota-tests-"+UUID().uuidString)
        try! FileManager.default.createDirectory(at:temp,withIntermediateDirectories:true);defer{try? FileManager.default.removeItem(at:temp)}
        let backup=temp.appendingPathComponent("original.json")
        let original:[String:Any]=["statusLine":["type":"command","command":"my-status-line","padding":2],"keep":true]
        let installed=try! AgentQuotaHook.configure(original,install:true,backup:backup)
        let again=try! AgentQuotaHook.configure(installed,install:true,backup:backup)
        let restored=try! AgentQuotaHook.configure(again,install:false,backup:backup)
        check(NSDictionary(dictionary:restored).isEqual(to:original),"one-click Claude integration restores existing status-line configuration")
        let capture=AgentQuotaHook.captureTime(id:first.id,owner:owner,now:now,directory:temp)
        let repeatCapture=AgentQuotaHook.captureTime(id:first.id,owner:owner,now:now.addingTimeInterval(1000),directory:temp)
        check(capture==now && repeatCapture==now,"persisted status-line observation cannot renew after a desktop restart")
        let active=result();let data=try! JSONSerialization.data(withJSONObject:active)
        check(!String(decoding:data,as:UTF8.self).contains("token"),"quota fixtures contain no credentials")
    }
}
