import Foundation
import Darwin

enum AgentTests {
    static func run() {
        func check(_ pass:@autoclosure()->Bool,_ name:String) { if !pass() { fatalError("Agent test: "+name) } }
        let payload:[String:Any]=["session_id":"session","cwd":"/tmp/project","turn_id":"turn","tool_name":"Bash"]
        for agent in AgentID.allCases {
            let event=AgentEvent.parse(agent:agent,event:agent == .gemini ? "AfterAgent" : "Stop",payload:payload)
            check(event?.kind == .completed && event?.project=="project","completion mapping "+agent.rawValue)
            let data=try! JSONEncoder().encode(event!)
            check(!String(data:data,encoding:.utf8)!.contains("/tmp"),"no full directory in normalized event")
        }
        check(AgentEvent.parse(agent:.gemini,event:"AfterTool",payload:payload)==nil,"tool success is not turn completion")
        check(AgentEvent.parse(agent:.claude,event:"PreToolUse",payload:payload)==nil,"ordinary tool not a question")
        var question=payload;question["tool_name"]="request_user_input_async"
        check(AgentEvent.parse(agent:.codex,event:"PreToolUse",payload:question)?.kind == .input,"Codex local question hook")
        question["parent_session_id"]="parent"
        check(AgentEvent.parse(agent:.codex,event:"Stop",payload:question)==nil,"child completion filtered")
        check(AgentEvent.parse(agent:.claude,event:"Notification",payload:["notification_type":"auth_success"])==nil,"ignore authentication notices")
        check(AgentEvent.parse(agent:.gemini,event:"Notification",payload:["notification_type":"ToolPermission"])?.kind == .approval,"Gemini approval")
        check(AgentEvent.parse(agent:.zcode,payload:["hookEventName":"Stop","sessionId":"s"])?.kind == .completed,"ZCode camelCase")
        check(AgentInstaller.events(.codex).count==3,"Codex installs only three alert callbacks")
        let legacyEvents=["SessionStart","UserPromptSubmit","PermissionRequest","Stop","PreToolUse","PostToolUse","Interrupt","SessionEnd"]
        let legacyHooks=Dictionary(uniqueKeysWithValues:legacyEvents.map { event in
            (event,[["hooks":[["type":"command","command":AgentInstaller.command(.codex,event)]]]])
        })
        let migrated=try! AgentInstaller.merged(["hooks":legacyHooks,"description":"Existing description"],agent:.codex,install:true)
        let migratedHooks=migrated["hooks"] as! [String:Any]
        check(Set(migratedHooks.keys)==Set(AgentInstaller.events(.codex)),"legacy lifecycle hooks removed during reinstall")
        check(migrated["description"] as? String=="Existing description","existing file description preserved")
        for event in AgentInstaller.events(.codex) {
            let groups=migratedHooks[event] as! [[String:Any]],handlers=groups[0]["hooks"] as! [[String:Any]]
            check(handlers[0]["statusMessage"] as? String==AgentInstaller.codexStatus(event),"Codex hook purpose is identifiable")
        }
        for id in [AgentID.codex,.claude,.qwen,.gemini,.zcode] {
            let old=id == .gemini ? ["SessionStart","BeforeAgent","AfterAgent","Notification","SessionEnd"] : id == .zcode ? ["SessionStart","UserPromptSubmit","PermissionRequest","Stop"] : legacyEvents
            let entries=Dictionary(uniqueKeysWithValues:old.map { event in (event,[["hooks":[["type":"command","command":AgentInstaller.command(id,event)]]]]) })
            let source:[String:Any]=id == .zcode ? ["hooks":["enabled":true,"events":entries]] : ["hooks":entries]
            let cleaned=try! AgentInstaller.merged(source,agent:id,install:true)["hooks"] as! [String:Any]
            let callbacks=id == .zcode ? cleaned["events"] as! [String:Any] : cleaned
            check(Set(callbacks.keys)==Set(AgentInstaller.events(id)),"old silent hooks removed for "+id.rawValue)
        }
        for event in ["SessionStart","UserPromptSubmit","BeforeAgent","PostToolUse","SessionEnd","Interrupt","started","resolved"] {
            check(AgentEvent.parse(agent:.codex,event:event,payload:payload)==nil,"ignore obsolete lifecycle callback "+event)
        }
        for agent in AgentID.allCases {
            check(AgentEvent.parse(agent:agent,event:"Notification",payload:["notification_type":"idle_prompt"])==nil,"idle is not a new question "+agent.rawValue)
        }
        check(AgentEvent.parse(agent:.claude,event:"Notification",payload:["notification_type":"permission_prompt"])==nil,"dedicated permission hook owns approval")
        check(AgentEvent.parse(agent:.claude,event:"Notification",payload:["notification_type":"elicitation_dialog"])?.kind == .input,"real MCP question retained")
        let center=AgentCenter(preview:true);center.enabled=true
        var delivered=0;center.deliver={_ in delivered+=1}
        let immediate=AgentEvent.parse(agent:.codex,event:"Stop",payload:payload)!
        center.accept(immediate)
        check(delivered==1,"completion delivered synchronously without debounce")
        center.accept(immediate)
        check(delivered==1,"immediate delivery retains duplicate filtering")
        var stale=immediate;stale.created-=20;center.accept(stale)
        check(delivered==1,"expired events remain ignored")
        let epoch=Date().timeIntervalSince1970
        func notice(_ agent:AgentID,_ session:String,_ kind:AgentKind,_ request:String)->AgentEvent {
            AgentEvent(agent:agent,kind:kind,session:session,
                       key:AgentEvent.digest(agent.rawValue+session+kind.rawValue+request),project:"",created:epoch)
        }
        for agent in AgentID.allCases {
            let gate=AgentCenter(preview:true);gate.enabled=true
            var received:[AgentKind]=[];gate.deliver={received.append($0.kind)}
            let first=notice(agent,"one",.input,"question")
            let overlap=notice(agent,"one",.approval,"permission")
            gate.accept(first,now:epoch)
            check(received==[.input],"first event is immediate "+agent.rawValue)
            gate.accept(overlap,now:epoch+0.2)
            gate.accept(notice(agent,"one",.completed,"stop"),now:epoch+0.8)
            check(received==[.input],"cross-kind overlap suppressed "+agent.rawValue)
            gate.accept(notice(agent,"two",.approval,"other"),now:epoch+0.9)
            check(received.count==2,"different session is independent "+agent.rawValue)
            gate.accept(notice(agent,"one",.approval,"next"),now:epoch+1)
            check(received.count==3,"one-second boundary does not slide "+agent.rawValue)
            gate.accept(overlap,now:epoch+2.1)
            check(received.count==3,"discarded callback retries stay deduplicated "+agent.rawValue)
        }
        let independent=AgentCenter(preview:true);independent.enabled=true
        var independentCount=0;independent.deliver={_ in independentCount+=1}
        independent.accept(notice(.claude,"same",.input,"a"),now:epoch)
        independent.accept(notice(.codex,"same",.approval,"b"),now:epoch+0.1)
        check(independentCount==2,"different agents sharing a session string stay independent")
        independent.accept(notice(.claude,"unknown",.approval,"c"),now:epoch+0.2)
        independent.accept(notice(.claude,"unknown",.input,"d"),now:epoch+0.3)
        independent.accept(notice(.claude,"",.input,"e"),now:epoch+0.4)
        independent.accept(notice(.claude,"",.approval,"f"),now:epoch+0.5)
        check(independentCount==6,"unknown identities never collapse unrelated sessions")
        for id in [AgentID.codex,.claude,.zcode,.gemini,.qwen] {
            let foreign:[String:Any]=["type":"command","command":"my-other-hook"]
            let originals:[String:Any]=id == .zcode ? ["mcp":["keep":true],"hooks":["enabled":false,"events":["Stop":[["hooks":[foreign]]]]]] : ["mcp":["keep":true],"hooks":["Stop":[["hooks":[foreign]]]]]
            let first=try! AgentInstaller.merged(originals,agent:id,install:true)
            let configured=first["hooks"] as! [String:Any]
            let configuredEvents=id == .zcode ? configured["events"] as! [String:Any] : configured
            let handler=configuredEvents[id == .gemini ? "AfterAgent" : "Stop"] as! [[String:Any]]
            let last=handler.last!["hooks"] as! [[String:Any]]
            check((last[0]["timeout"] as? Int)==(id == .zcode ? nil : (id == .gemini || id == .qwen ? 2000 : 2)),"native timeout units "+id.rawValue)
            let second=try! AgentInstaller.merged(first,agent:id,install:true)
            check(NSDictionary(dictionary:first).isEqual(to:second),"idempotent install "+id.rawValue)
            let removed=try! AgentInstaller.merged(second,agent:id,install:false)
            check((removed["mcp"] as? [String:Bool])?["keep"]==true,"other config preserved")
            let h=removed["hooks"] as! [String:Any],events=id == .zcode ? h["events"] as! [String:Any] : h
            check((events["Stop"] as? [[String:Any]])?.count==1,"uninstall keeps foreign hooks")
            check(!String(data:try! JSONSerialization.data(withJSONObject:removed),encoding:.utf8)!.contains(AgentInstaller.marker),"owned hooks removed")
        }
        let toml="model = 'custom'\n[features]\nhooks = false\nother = true\n[projects.foo]\ntrust_level = 'trusted'"
        let enabled=try! AgentInstaller.enableCodex(toml)
        check(enabled.contains("hooks = true") && enabled.contains("other = true") && enabled.contains("trust_level = 'trusted'"),"TOML preserves unrelated keys")
        check(try! AgentInstaller.enableCodex(enabled)==enabled,"TOML idempotence")
        let malformed:[String:Any]=["hooks":["Stop":"broken"]]
        check((try? AgentInstaller.merged(malformed,agent:.claude,install:true))==nil,"reject malformed config before writes")
        let fixtures=FileManager.default.temporaryDirectory.appendingPathComponent("vibe-detect-"+UUID().uuidString)
        defer { try? FileManager.default.removeItem(at:fixtures) }
        func writeJSON(_ value:Any,_ file:URL) {
            try! AgentInstaller.privateWrite(try! JSONSerialization.data(withJSONObject:value),to:file,backup:false)
        }
        let config=fixtures.appendingPathComponent("settings.json")
        check(!AgentInstaller.installation(.claude,file:config).installed,"missing config is not installed")
        writeJSON(["description":AgentInstaller.marker+" claude Stop"],config)
        check(!AgentInstaller.installation(.claude,file:config).installed,"marker in unrelated text is not a Hook")
        let hooks=try! AgentInstaller.merged([:],agent:.claude,install:true)
        writeJSON(hooks,config)
        check(AgentInstaller.installation(.claude,file:config).installed,"installed Hook survives fresh detection without an event")
        var disabled=hooks;disabled["disableAllHooks"]=true;writeJSON(disabled,config)
        check(!AgentInstaller.installation(.claude,file:config).issue.isEmpty,"disabled Hooks are abnormal")
        try! AgentInstaller.privateWrite(Data("broken json".utf8),to:config,backup:false)
        check(!AgentInstaller.installation(.claude,file:config).issue.isEmpty,"unreadable config is abnormal rather than undetected")
        let profiles=fixtures.appendingPathComponent("profiles"),desktop=profiles.appendingPathComponent("desktop")
        let manifest=desktop.appendingPathComponent("package.json")
        let plugin=fixtures.appendingPathComponent("prepared/vibe-companion-dsh")
        let name="vibe-companion-dsh"
        writeJSON(["name":name,"main":"index.js","dsh":["bundle":["patch":"cordis.patch.yml"]]],plugin.appendingPathComponent("package.json"))
        try! AgentInstaller.privateWrite(Data("export default {};".utf8),to:plugin.appendingPathComponent("index.js"),backup:false)
        try! AgentInstaller.privateWrite(Data("- insert: []".utf8),to:plugin.appendingPathComponent("cordis.patch.yml"),backup:false)
        check(!AgentInstaller.dshInstallation(profiles:profiles).installed,"preparing DSH package alone is not installation")
        let native:[String:Any]=["dependencies":[name:"link:"+plugin.path],"dsh":["profile":["bundles":[name]]]]
        writeJSON(native,manifest)
        check(!AgentInstaller.dshInstallation(profiles:profiles).issue.isEmpty,"DSH declared but missing package is abnormal")
        let modules=desktop.appendingPathComponent("node_modules")
        try! FileManager.default.createDirectory(at:modules,withIntermediateDirectories:true)
        try! FileManager.default.createSymbolicLink(at:modules.appendingPathComponent(name),withDestinationURL:plugin)
        check(AgentInstaller.dshInstallation(profiles:profiles).installed,"native desktop DSH bundle and installed symlink are recognized after restart")
        writeJSON(["dependencies":[name:"link:"+plugin.path],"dsh":["profile":["bundles":[]]]],manifest)
        check(!AgentInstaller.dshInstallation(profiles:profiles).issue.isEmpty,"dependency without DSH bundle is not active installation")
        writeJSON(native,profiles.appendingPathComponent("web/package.json"))
        let webModules=profiles.appendingPathComponent("web/node_modules")
        try! FileManager.default.createDirectory(at:webModules,withIntermediateDirectories:true)
        try! FileManager.default.createSymbolicLink(at:webModules.appendingPathComponent(name),withDestinationURL:plugin)
        check(AgentInstaller.dshInstallation(profiles:profiles).installed,"valid terminal profile remains installed independently of desktop profile")
        try! FileManager.default.removeItem(at:plugin.appendingPathComponent("index.js"))
        check(!AgentInstaller.dshInstallation(profiles:profiles).installed,"broken plugin link is abnormal rather than installed")
        writeJSON(["dependencies":[:],"dsh":["profile":["bundles":[]]]],manifest)
        writeJSON(["dependencies":[:],"dsh":["profile":["bundles":[]]]],profiles.appendingPathComponent("web/package.json"))
        check(!AgentInstaller.dshInstallation(profiles:profiles).installed && AgentInstaller.dshInstallation(profiles:profiles).issue.isEmpty,"removing native registration resets installation despite leftover files")
        check(AgentIntegration(agent:.dsh,executable:nil,application:nil,installed:true).status=="已安装","integration status does not depend on received events")
        check(AgentIntegration(agent:.dsh,executable:nil,application:nil,installed:false).status=="未安装","not installed status")
        check(AgentIntegration(agent:.dsh,executable:nil,application:nil,installed:true,issue:"broken").status=="异常","abnormal status takes precedence")
        let file=FileManager.default.temporaryDirectory.appendingPathComponent("vibe-agent-config-"+UUID().uuidString)
        defer { try? FileManager.default.removeItem(at:file) }
        try! AgentInstaller.privateWrite(Data("original".utf8),to:file,backup:false)
        check((try! FileManager.default.attributesOfItem(atPath:file.path)[.posixPermissions] as? NSNumber)?.intValue==0o600,"endpoint/config private")
        let link=file.appendingPathExtension("link")
        try! FileManager.default.createSymbolicLink(at:link,withDestinationURL:file)
        try! AgentInstaller.privateWrite(Data("updated".utf8),to:link,backup:false)
        check((try? FileManager.default.destinationOfSymbolicLink(atPath:link.path)) != nil,"symlink preserved")
        check((try? String(contentsOf:file,encoding:.utf8))=="updated","symlink target updated")
        try! FileManager.default.removeItem(at:link)
        // Integration test: real ephemeral sockets, helper subprocess, endpoint replacement.
        var firstCount=0,secondCount=0
        let endpoint=file.appendingPathExtension("endpoint")
        let first=AgentReceiver(endpoint:endpoint),second=AgentReceiver(endpoint:endpoint)
        try! first.start{_ in firstCount+=1}
        let old=try! Data(contentsOf:endpoint)
        try! second.start{_ in secondCount+=1}
        let replacement=try! Data(contentsOf:endpoint)
        check(old != replacement,"port/token rotate on restart")
        let process=Process();process.executableURL=Bundle.main.executableURL
        process.arguments=[AgentInstaller.marker,"codex","Stop"]
        var environment=ProcessInfo.processInfo.environment;environment["VIBE_AGENT_ENDPOINT"]=endpoint.path;process.environment=environment
        let input=Pipe(),output=Pipe();process.standardInput=input;process.standardOutput=output
        try! process.run();input.fileHandleForWriting.write(try! JSONSerialization.data(withJSONObject:payload));try! input.fileHandleForWriting.close();process.waitUntilExit()
        check(process.terminationStatus==0,"hook process succeeds")
        check(String(data:output.fileHandleForReading.readDataToEndOfFile(),encoding:.utf8)?.trimmingCharacters(in:.whitespacesAndNewlines)=="{}","hook output does not change Agent decision")
        let deadline=Date().addingTimeInterval(2)
        while secondCount==0 && Date()<deadline { RunLoop.current.run(until:Date().addingTimeInterval(0.01)) }
        check(secondCount==1 && firstCount==0,"helper rereads new endpoint after port change")
        withExtendedLifetime((first,second)){}
        print("Agent host tests: PASS (seven mappings, config merge/uninstall, real helper, dynamic IPC port)")
    }
}
