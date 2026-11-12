import Foundation
import Darwin

struct AgentIntegration:Identifiable {
    let agent:AgentID,executable:URL?,application:URL?,installed:Bool
    var issue=""
    var id:String { agent.rawValue }
    var detected:Bool { executable != nil || application != nil }
    var status:String { !issue.isEmpty ? "异常" : installed ? "已安装" : "未安装" }
}
struct AgentInstallError:LocalizedError {
    let text:String
    var errorDescription:String? { text }
}
enum AgentInstaller {
    static let marker="--vibe-agent-hook"
    static var helper:URL { PairingStore.directory.appendingPathComponent("agent-bridge") }
    static func home(_ key:String,_ fallback:String)->URL {
        if let configured=ProcessInfo.processInfo.environment[key],!configured.isEmpty { return URL(fileURLWithPath:configured,isDirectory:true) }
        return FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(fallback,isDirectory:true)
    }
    static func config(_ agent:AgentID)->URL {
        switch agent {
        case .codex:return home("CODEX_HOME",".codex").appendingPathComponent("hooks.json")
        case .claude:return home("CLAUDE_CONFIG_DIR",".claude").appendingPathComponent("settings.json")
        case .zcode:return home("ZCODE_HOME",".zcode").appendingPathComponent("cli/config.json")
        case .gemini:
            let base=ProcessInfo.processInfo.environment["GEMINI_CLI_HOME"].map{URL(fileURLWithPath:$0).appendingPathComponent(".gemini")} ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".gemini")
            return base.appendingPathComponent("settings.json")
        case .qwen:return home("QWEN_CODE_HOME",".qwen").appendingPathComponent("settings.json")
        case .opencode:
            let base=home("XDG_CONFIG_HOME",".config").appendingPathComponent("opencode")
            let commented=base.appendingPathComponent("opencode.jsonc")
            return FileManager.default.fileExists(atPath:commented.path) ? commented : base.appendingPathComponent("opencode.json")
        case .dsh:return home("DSH_HOME",".dsh").appendingPathComponent("profiles/desktop/package.json")
        }
    }
    static func executable(_ names:[String])->URL? {
        let user=FileManager.default.homeDirectoryForCurrentUser.path
        let paths=(ProcessInfo.processInfo.environment["PATH"] ?? "").components(separatedBy:":")+["/opt/homebrew/bin","/usr/local/bin",user+"/.local/bin",user+"/.bun/bin",user+"/.npm-global/bin",user+"/.opencode/bin",user+"/.cargo/bin"]
        for name in names { for path in paths where !path.isEmpty { let u=URL(fileURLWithPath:path).appendingPathComponent(name);if FileManager.default.isExecutableFile(atPath:u.path) { return u } } }
        return nil
    }
    static func detect()->[AgentIntegration] {
        AgentID.allCases.map { id in
            let apps:[String]
            switch id {case .codex:apps=["Codex"];case .claude:apps=["Claude"];case .opencode:apps=["OpenCode","OpenCode Dev"];case .dsh:apps=["DeepSeek Harness"];case .zcode:apps=["ZCode"];default:apps=[]}
            let roots=[URL(fileURLWithPath:"/Applications"),FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Applications")]
            let app=roots.flatMap{root in apps.map{root.appendingPathComponent($0+".app")}}.first{FileManager.default.fileExists(atPath:$0.path)}
            let cli=executable([id == .claude ? "claude" : id.rawValue])
            let status=id == .dsh ? dshInstallation(profiles:home("DSH_HOME",".dsh").appendingPathComponent("profiles")) : installation(id,file:config(id))
            return AgentIntegration(agent:id,executable:cli,application:app,installed:status.installed,issue:status.issue)
        }
    }
    // Installation is read from native configuration, never inferred from a
    // previous event or from our prepared-but-not-installed plugin directory.
    static func installation(_ agent:AgentID,file:URL)->(installed:Bool,issue:String) {
        guard FileManager.default.fileExists(atPath:file.path) else { return (false,"") }
        do {
            let root=try dictionary(JSONSerialization.jsonObject(with:Data(contentsOf:file),options:.json5Allowed),"配置")
            if agent == .opencode {
                guard root["plugins"]==nil || root["plugins"] is [Any] else { return (false,"plugins 配置格式异常") }
                let installed=(root["plugins"] as? [Any] ?? []).contains { item in
                    let path=(item as? String) ?? (item as? [String:Any])?["package"] as? String ?? ""
                    return URL(fileURLWithPath:path).lastPathComponent=="vibe-companion-opencode"
                }
                return (installed,"")
            }
            let hooks=try dictionary(root["hooks"],"hooks")
            let events=agent == .zcode ? try dictionary(hooks["events"],"hooks.events") : hooks
            let installed=events.values.contains { value in
                (value as? [[String:Any]] ?? []).contains { group in
                    (group["hooks"] as? [[String:Any]] ?? []).contains {
                        ($0["command"] as? String ?? "").contains(marker+" "+agent.rawValue+" ")
                    }
                }
            }
            let disabled=root["disableAllHooks"] as? Bool==true || hooks["enabled"] as? Bool==false ||
                (agent == .gemini && (root["hooksConfig"] as? [String:Any])?["enabled"] as? Bool==false)
            return (installed,installed && disabled ? "Agent 中的 Hook 已禁用" : "")
        } catch { return (false,"无法读取或解析 Agent 配置") }
    }
    static func dshInstallation(profiles:URL)->(installed:Bool,issue:String) {
        let fm=FileManager.default,name="vibe-companion-dsh"
        guard fm.fileExists(atPath:profiles.path) else { return (false,"") }
        do {
            let folders=try fm.contentsOfDirectory(at:profiles,includingPropertiesForKeys:[.isDirectoryKey],options:.skipsHiddenFiles)
            var installed=false,issue=""
            for folder in folders where folder.lastPathComponent != "node_modules" {
                guard try folder.resourceValues(forKeys:[.isDirectoryKey]).isDirectory==true else { continue }
                let file=folder.appendingPathComponent("package.json")
                guard fm.fileExists(atPath:file.path) else { continue }
                do {
                    let root=try dictionary(JSONSerialization.jsonObject(with:Data(contentsOf:file)),"profile")
                    let profile=(root["dsh"] as? [String:Any])?["profile"] as? [String:Any]
                    let declared=(root["dependencies"] as? [String:Any])?[name] != nil
                    let bundled=(profile?["bundles"] as? [String] ?? []).contains(name)
                    guard declared || bundled else { continue }
                    guard bundled else { issue="DSH 插件尚未加入配置，请在 DSH 中重新安装";continue }
                    let package=folder.appendingPathComponent("node_modules/"+name)
                    let manifest=try dictionary(JSONSerialization.jsonObject(with:Data(contentsOf:package.appendingPathComponent("package.json"))),"插件")
                    let entry=(manifest["main"] as? String) ?? "index.js"
                    let bundle=(manifest["dsh"] as? [String:Any])?["bundle"] as? [String:Any]
                    guard manifest["name"] as? String==name,let patch=bundle?["patch"] as? String,
                          fm.fileExists(atPath:package.appendingPathComponent(entry).path),
                          fm.fileExists(atPath:package.appendingPathComponent(patch).path) else {
                        issue="DSH 插件文件不完整，请在 DSH 中重新安装";continue
                    }
                    installed=true
                } catch { issue="无法读取 DSH 插件配置或文件，请在 DSH 中检查插件" }
            }
            // Desktop and terminal profiles are independent. One valid installed
            // profile is enough; never turn an unrelated profile into an error.
            return (installed,installed ? "" : issue)
        } catch { return (false,"无法读取 DSH 配置目录") }
    }
    static func privateWrite(_ data:Data,to file:URL,backup:Bool=true)throws {
        let file=file.resolvingSymlinksInPath()
        let fm=FileManager.default,folder=file.deletingLastPathComponent()
        try fm.createDirectory(at:folder,withIntermediateDirectories:true,attributes:[.posixPermissions:0o700])
        if backup,fm.fileExists(atPath:file.path) {
            let saved=file.appendingPathExtension("vibe-backup-"+UUID().uuidString)
            try privateWrite(try Data(contentsOf:file),to:saved,backup:false)
        }
        let temporary=folder.appendingPathComponent(".vibe-"+UUID().uuidString)
        guard fm.createFile(atPath:temporary.path,contents:data,attributes:[.posixPermissions:0o600]) else { throw AgentInstallError(text:"无法写入 "+folder.path) }
        defer { try? fm.removeItem(at:temporary) }
        guard Darwin.rename(temporary.path,file.path)==0 else { throw AgentInstallError(text:"配置保存失败，原文件未修改") }
    }
    static func prepareHelper()throws {
        guard let binary=Bundle.main.executableURL else { throw AgentInstallError(text:"无法定位通知接收程序") }
        let content=try Data(contentsOf:binary)
        if (try? Data(contentsOf:helper)) != content { try privateWrite(content,to:helper,backup:false);try FileManager.default.setAttributes([.posixPermissions:0o700],ofItemAtPath:helper.path) }
    }
    static func refreshPreparedPlugins()throws {
        for source in ["opencode","dsh"] {
            let name="vibe-companion-"+source
            let target=PairingStore.directory.appendingPathComponent("agents/"+name)
            if FileManager.default.fileExists(atPath:target.path) { _=try stage(source,name:name) }
        }
    }
    static func quote(_ text:String)->String { "'"+text.replacingOccurrences(of:"'",with:"'\\''")+"'" }
    static func command(_ id:AgentID,_ event:String)->String { quote(helper.path)+" "+marker+" "+id.rawValue+" "+event }
    static func events(_ id:AgentID)->[String] {
        switch id {
        case .gemini:return ["AfterAgent","Notification"]
        case .zcode:return ["PermissionRequest","Stop"]
        case .claude:return ["PermissionRequest","Notification","Stop","PreToolUse"]
        default:return ["PermissionRequest","Stop","PreToolUse"]
        }
    }
    static func codexStatus(_ event:String)->String {
        switch event {
        case "Stop":return "Vibe 伴侣 · 完成提醒"
        case "PermissionRequest":return "Vibe 伴侣 · 等待审批"
        case "PreToolUse":return "Vibe 伴侣 · 等待回答"
        default:return "Vibe 伴侣 · Agent 提醒"
        }
    }
    static func dictionary(_ value:Any?,_ path:String)throws->[String:Any] {
        guard let value=value else { return [:] }
        guard let result=value as? [String:Any] else { throw AgentInstallError(text:path+" 不是对象，已保留原配置") };return result
    }
    static func merged(_ original:[String:Any],agent:AgentID,install:Bool)throws->[String:Any] {
        var root=original
        var hooks=try dictionary(root["hooks"],"hooks")
        var entries=agent == .zcode ? try dictionary(hooks["events"],"hooks.events") : hooks
        if agent == .zcode {
            let valid:Set<String>=["SessionStart","UserPromptSubmit","PreToolUse","PermissionRequest","PostToolUse","PostToolUseFailure","Stop"]
            guard Set(hooks.keys).subtracting(["events","enabled"]).isEmpty,Set(entries.keys).isSubset(of:valid) else { throw AgentInstallError(text:"ZCode 的 hooks 格式不兼容，原配置未修改") }
        }
        for event in Set(entries.keys).union(events(agent)) {
            guard entries[event]==nil || entries[event] is [[String:Any]] else { throw AgentInstallError(text:"hooks."+event+" 不是数组，原配置未修改") }
            var groups=entries[event] as? [[String:Any]] ?? []
            groups=groups.compactMap { group in
                guard let handlers=group["hooks"] as? [[String:Any]] else { return group }
                let kept=handlers.filter{!(($0["command"] as? String ?? "").contains(marker+" "+agent.rawValue+" "))}
                if kept.isEmpty { return nil };var copy=group;copy["hooks"]=kept;return copy
            }
            if install,events(agent).contains(event) {
                var handler:[String:Any]=["type":"command","command":command(agent,event)]
                if agent != .zcode { handler["timeout"]=(agent == .gemini || agent == .qwen) ? 2000 : 2 }
                if agent == .codex { handler["statusMessage"]=codexStatus(event) }
                if agent == .gemini { handler["name"]="vibe-companion-"+event }
                var group:[String:Any]=["hooks":[handler]]
                if event=="PreToolUse" { group["matcher"]="^(AskUserQuestion|ExitPlanMode|request_user_input|request_user_input_async|ask_user_question|exit_plan_mode)$" }
                if agent == .claude,event=="Notification" { group["matcher"]="^(elicitation_dialog|elicitation_url_dialog)$" }
                groups.append(group)
            }
            if groups.isEmpty { entries.removeValue(forKey:event) } else { entries[event]=groups }
        }
        if agent == .zcode { hooks["events"]=entries;if install { hooks["enabled"]=true };root["hooks"]=hooks }
        else { root["hooks"]=entries }
        if agent == .codex,install,root["description"]==nil { root["description"]="Vibe 伴侣 · Agent 提醒" }
        if agent == .gemini,install { var configuration=try dictionary(root["hooksConfig"],"hooksConfig");configuration["enabled"]=true;root["hooksConfig"]=configuration }
        return root
    }
    static func enableCodex(_ text:String)throws->String {
        // Change only [features].hooks; leave inline hooks, notify, trust and all other TOML untouched.
        let lines=text.components(separatedBy:"\n");var output=[String](),inFeatures=false,found=false,hasSection=false
        for line in lines {
            let clean=line.trimmingCharacters(in:.whitespaces)
            if clean.hasPrefix("[") {
                if inFeatures && !found { output.append("hooks = true");found=true }
                inFeatures=clean.range(of:#"^\[features\]\s*(#.*)?$"#,options:.regularExpression) != nil
                hasSection = hasSection || inFeatures
            }
            if inFeatures,clean.range(of:#"^hooks\s*="#,options:.regularExpression) != nil { output.append("hooks = true");found=true }
            else { output.append(line) }
        }
        if inFeatures && !found { output.append("hooks = true") }
        if !hasSection {
            if text.range(of:#"(?m)^\s*features\s*="#,options:.regularExpression) != nil { throw AgentInstallError(text:"Codex 使用了内联 features 配置，请在 Codex 中开启 Hooks；原配置未修改") }
            output.append("\n[features]\nhooks = true")
        }
        return output.joined(separator:"\n")
    }
    static func change(_ row:AgentIntegration,install:Bool)throws->String {
        if install { try prepareHelper() }
        if row.agent == .dsh {
            let target=try stage("dsh",name:"vibe-companion-dsh")
            if install,let cli=row.executable {
                try run(cli,args:["plugin","--profile","web","add",target.path])
                return "终端插件已安装。桌面版请在 DSH 插件管理中从本地目录添加："+target.path
            }
            return "在 DeepSeek Harness 的插件管理中，从本地目录安装或移除："+target.path
        }
        let file=config(row.agent)
        let data=FileManager.default.fileExists(atPath:file.path) ? try Data(contentsOf:file) : Data("{}".utf8)
        let object:Any
        do { object=try JSONSerialization.jsonObject(with:data,options:.json5Allowed) }
        catch { throw AgentInstallError(text:"配置无法解析，未修改："+file.path) }
        var root=try dictionary(object,file.lastPathComponent)
        if row.agent == .opencode {
            let target=PairingStore.directory.appendingPathComponent("agents/vibe-companion-opencode")
            if install { _=try stage("opencode",name:"vibe-companion-opencode") }
            guard root["plugins"]==nil || root["plugins"] is [Any] else { throw AgentInstallError(text:"OpenCode plugins 不是数组，原配置未修改") }
            var plugins=root["plugins"] as? [Any] ?? []
            plugins.removeAll { ($0 as? String)==target.path || ($0 as? [String:Any])?["package"] as? String==target.path }
            if install { plugins.append(target.path) };root["plugins"]=plugins
        } else { root=try merged(root,agent:row.agent,install:install) }
        var toml:URL?,tomlData:Data?
        if row.agent == .codex,install {
            toml=file.deletingLastPathComponent().appendingPathComponent("config.toml")
            let original=(try? String(contentsOf:toml!,encoding:.utf8)) ?? ""
            tomlData=Data(try enableCodex(original).utf8)
        }
        // Parse and validate every target before writing. Backups are account-private.
        let encoded=try JSONSerialization.data(withJSONObject:root,options:[.prettyPrinted,.sortedKeys,.withoutEscapingSlashes])
        if let toml=toml,let content=tomlData { try privateWrite(content,to:toml) }
        try privateWrite(encoded,to:file)
        if !install { return "已移除 Vibe 伴侣集成，其他配置已保留" }
        if row.agent == .codex { return "已安装。请在 Codex 的 /hooks 或桌面端「设置 → Hooks」中信任新 Hook。" }
        return "已安装。请重新打开对应 Agent 会话。"
    }
    static func stage(_ source:String,name:String)throws->URL {
        guard let resources=Bundle.main.resourceURL else { throw AgentInstallError(text:"插件资源缺失") }
        let folder=resources.appendingPathComponent("AgentAdapters/"+source),target=PairingStore.directory.appendingPathComponent("agents/"+name)
        guard let iterator=FileManager.default.enumerator(at:folder,includingPropertiesForKeys:nil) else { throw AgentInstallError(text:"插件资源缺失，请重新安装应用") }
        for case let file as URL in iterator {
            guard (try file.resourceValues(forKeys:[.isRegularFileKey])).isRegularFile==true else { continue }
            let relative=String(file.path.dropFirst(folder.path.count+1))
            var content=try Data(contentsOf:file)
            if file.pathExtension != "tgz",let text=String(data:content,encoding:.utf8) {
                let quoted=String(data:try JSONSerialization.data(withJSONObject:[helper.path]),encoding:.utf8)!
                let literal=String(quoted.dropFirst().dropLast())
                content=Data(text.replacingOccurrences(of:"\"__VIBE_HELPER__\"",with:literal).utf8)
            }
            try privateWrite(content,to:target.appendingPathComponent(relative),backup:false)
        }
        return target
    }
    static func run(_ binary:URL,args:[String])throws {
        let task=Process();task.executableURL=binary;task.arguments=args
        // Avoid a filled output pipe stalling a package manager; keep a private local log.
        let log=PairingStore.directory.appendingPathComponent("agent-install.log")
        try privateWrite(Data(),to:log,backup:false)
        let handle=try FileHandle(forWritingTo:log);defer{try? handle.close()}
        task.standardOutput=handle;task.standardError=handle
        var environment=ProcessInfo.processInfo.environment
        environment["PATH"]="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:"+(environment["PATH"] ?? "")
        task.environment=environment
        try task.run();let deadline=Date().addingTimeInterval(55)
        while task.isRunning && Date()<deadline { Thread.sleep(forTimeInterval:0.1) }
        if task.isRunning { task.terminate();throw AgentInstallError(text:"插件安装超时，请检查网络或在 Agent 中安装本地插件") }
        guard task.terminationStatus==0 else { throw AgentInstallError(text:"Agent 拒绝了插件安装，请在该 Agent 的插件管理中安装本地插件。诊断日志："+log.path) }
    }
}
