import SwiftUI
import AppKit
import os

final class AppDelegate:NSObject,NSApplicationDelegate,NSWindowDelegate {
    var model:AppModel?,window:NSWindow?,item:NSStatusItem?
    private var permissionReloaded=false,permissionProbeRunning=false,awaitingPermissionReturn=false
    private var sleepResume=SleepResumePolicy()
    private let powerLog=Logger(subsystem:"io.github.vibecompanion.mac",category:"power")
    func applicationDidFinishLaunching(_ notification:Notification) {
        guard !CommandLine.arguments.contains("--self-test") else { return }
        let preview=CommandLine.arguments.contains("--preview-ui") || Bundle.main.bundleIdentifier?.hasSuffix(".preview")==true
        var resumed:PermissionSession?
        if let i=CommandLine.arguments.firstIndex(of:"--resume-permission"),CommandLine.arguments.count>i+1 { resumed=PermissionSessionStore.consume(CommandLine.arguments[i+1]) }
        permissionReloaded=resumed != nil
        let peers=NSRunningApplication.runningApplications(withBundleIdentifier:Bundle.main.bundleIdentifier ?? "io.github.vibecompanion.mac")
        if !preview,resumed==nil,let existing=peers.first(where:{$0.processIdentifier != ProcessInfo.processInfo.processIdentifier}) { existing.activate(options:[.activateAllWindows]);NSApp.terminate(nil);return }
        let m=AppModel(preview:preview,connectionPaused:resumed?.connectionPaused ?? false);model=m
        if let resumed=resumed { m.restore(resumed);m.result="快捷键授权已重新载入" }
        m.onPermissionRequested={ [weak self] in self?.permissionReloaded=false;self?.awaitingPermissionReturn=true }
        m.onConnectionRequested={ [weak self] in self?.resumeForUserInteraction() }
        let main=NSMenu()
        let application=NSMenuItem();let applicationMenu=NSMenu();application.submenu=applicationMenu
        applicationMenu.addItem(withTitle:"关于 Vibe 伴侣",action:#selector(NSApplication.orderFrontStandardAboutPanel(_:)),keyEquivalent:"")
        applicationMenu.addItem(withTitle:"关闭窗口",action:#selector(closeWindow),keyEquivalent:"w").target=self
        applicationMenu.addItem(.separator())
        applicationMenu.addItem(withTitle:"退出 Vibe 伴侣",action:#selector(quit),keyEquivalent:"q").target=self
        main.addItem(application)
        let edit=NSMenuItem();let editMenu=NSMenu(title:"编辑");edit.submenu=editMenu
        editMenu.addItem(withTitle:"剪切",action:#selector(NSText.cut(_:)),keyEquivalent:"x")
        editMenu.addItem(withTitle:"复制",action:#selector(NSText.copy(_:)),keyEquivalent:"c")
        editMenu.addItem(withTitle:"粘贴",action:#selector(NSText.paste(_:)),keyEquivalent:"v")
        editMenu.addItem(withTitle:"全选",action:#selector(NSText.selectAll(_:)),keyEquivalent:"a")
        main.addItem(edit);NSApp.mainMenu=main
        let w=NSWindow(contentRect:NSRect(x:0,y:0,width:900,height:700),styleMask:[.titled,.closable,.miniaturizable,.resizable],backing:.buffered,defer:false)
        w.title="Vibe 伴侣";w.delegate=self;w.minSize=NSSize(width:820,height:640);w.titlebarAppearsTransparent=true;w.backgroundColor=NSColor(srgbRed:0.065,green:0.10,blue:0.11,alpha:1);w.contentView=NSHostingView(rootView:CompanionView(model:m));w.center();w.isReleasedWhenClosed=false;window=w
        if resumed?.visible != false { show() }
        let status=NSStatusBar.system.statusItem(withLength:NSStatusItem.squareLength);status.button?.image=CompanionBrand.image();status.button?.toolTip="Vibe 伴侣"
        let menu=NSMenu();menu.addItem(withTitle:"打开 Vibe 伴侣",action:#selector(show),keyEquivalent:"").target=self
        menu.addItem(.separator());menu.addItem(withTitle:"退出",action:#selector(quit),keyEquivalent:"q").target=self;status.menu=menu;item=status
        NSWorkspace.shared.notificationCenter.addObserver(self,selector:#selector(willSleep),name:NSWorkspace.willSleepNotification,object:nil)
        NSWorkspace.shared.notificationCenter.addObserver(self,selector:#selector(didWake),name:NSWorkspace.didWakeNotification,object:nil)
    }
    func applicationDidBecomeActive(_ notification:Notification) { refreshPermission() }
    func applicationDidResignActive(_ notification:Notification) { permissionReloaded=false }
    func windowDidBecomeKey(_ notification:Notification) { refreshPermission() }
    private func refreshPermission() {
        guard !sleepResume.waitingForWake,let m=model,!m.preview,!permissionProbeRunning else { return }
        // PostEvent preflight can cache both grants and denials. Check a fresh
        // process even when this instance still reports an old grant.
        guard !permissionReloaded else { m.engine.retry(resume:false);return } // One reload per activation.
        permissionProbeRunning=true;m.permissionChecking=true
        let powerGeneration=sleepResume.generation
        DispatchQueue.global(qos:.utility).async { [weak self] in
            let granted=PermissionProbe.check()
            DispatchQueue.main.async {
                guard let self=self,let m=self.model else { return }
                self.permissionProbeRunning=false;m.permissionChecking=false
                guard self.sleepResume.acceptsCompletion(from:powerGeneration) else {
                    self.refreshPermission();return
                }
                guard PermissionRecoveryPolicy.shouldReload(current:Shortcut.permitted,fresh:granted,returnedFromSettings:self.awaitingPermissionReturn,alreadyReloaded:self.permissionReloaded) else {
                    if Shortcut.permitted { self.awaitingPermissionReturn=false }
                    m.engine.retry(resume:false);return
                }
                // Defer the reload until in-flight saves/probes/capture finish.
                if m.saving || m.radioLoading || m.radioChecking || m.state.recording {
                    DispatchQueue.main.asyncAfter(deadline:.now()+1) { [weak self] in self?.refreshPermission() };return
                }
                self.reloadAfterPermission()
            }
        }
    }
    private func reloadAfterPermission() {
        guard !sleepResume.waitingForWake else { return }
        guard let m=model,let id=PermissionSessionStore.save(m.permissionSession(visible:awaitingPermissionReturn || window?.isVisible==true)) else {
            model?.result="授权已开启，但无法保留当前草稿；请保存后重新打开应用";return
        }
        permissionReloaded=true;awaitingPermissionReturn=false;m.permissionChecking=true;m.result="正在重新检测快捷键…"
        let powerGeneration=sleepResume.generation
        m.engine.stop { [weak self] in
            guard let self=self else { PermissionSessionStore.discard(id);return }
            guard self.sleepResume.acceptsCompletion(from:powerGeneration) else {
                PermissionSessionStore.discard(id);self.permissionReloaded=false
                m.permissionChecking=false;m.result="";self.refreshPermission();return
            }
            if PermissionRelaunch.schedule(resumeID:id) { NSApp.terminate(nil) }
            else {
                PermissionSessionStore.discard(id);self.model?.permissionChecking=false
                self.model?.result="无法重新载入快捷键权限，请稍后重试";self.model?.engine.start()
            }
        }
    }

    func windowWillClose(_ notification:Notification) { NSApp.hide(nil) }
    @objc func closeWindow() { window?.performClose(nil) }
    @objc func show() { resumeForUserInteraction();NSApp.unhide(nil);window?.makeKeyAndOrderFront(nil);NSApp.activate(ignoringOtherApps:true);refreshPermission() }
    @objc func quit() { NSApp.terminate(nil) }
    @objc func willSleep() {
        powerLog.notice("System sleep: suspending device links and quota polling")
        sleepResume.willSleep();model?.agents.quota.suspend();model?.engine.suspendForSleep()
    }
    @objc func didWake() {
        let resume=sleepResume.didWake()
        powerLog.notice("System wake resumeLinks=\(resume)")
        if resume { resumeDeviceWork() }
    }
    private func resumeForUserInteraction() {
        if sleepResume.userRequestedResume() {
            powerLog.notice("User interaction: resuming device links and quota polling")
            resumeDeviceWork()
        }
    }
    private func resumeDeviceWork() {
        guard let model=model,!model.preview else { return }
        model.agents.quota.resume();model.engine.resumeAfterSleep();refreshPermission()
    }
    func applicationWillTerminate(_ notification:Notification) { model?.engine.stop() }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender:NSApplication)->Bool { false }
    func applicationShouldHandleReopen(_ sender:NSApplication,hasVisibleWindows:Bool)->Bool { show();return true }
}

@main struct VibeMain {
    static func main() {
        if CommandLine.arguments.contains("--notify") {AgentBridge.run(quota:false);return}
        if CommandLine.arguments.contains("--quota") {AgentBridge.run(quota:true);return}
        if let i=CommandLine.arguments.firstIndex(of:"--vibe-quota-hook"),CommandLine.arguments.count>i+1,CommandLine.arguments[i+1]=="opencode" {AgentBridge.run(quota:true,builtin:.opencode);return}
        if CommandLine.arguments.contains("--vibe-quota-statusline") { AgentQuotaHook.run();return }
        if CommandLine.arguments.contains("--agent-quota-probe") {
            guard let executable=AgentInstaller.executable(["codex"]),let sample=try? CodexQuotaProbe.read(executable:executable) else { print("unavailable");exit(1) }
            let report:[String:Any]=["agent":sample.agent.rawValue,"items":sample.items.map{["label":$0.title,"remaining":$0.remaining,"minutes":$0.minutes]}]
            if let data=try? JSONSerialization.data(withJSONObject:report,options:.sortedKeys) { print(String(decoding:data,as:UTF8.self)) };return
        }
        if let i=CommandLine.arguments.firstIndex(of:AgentInstaller.marker) { AgentReceiver.runHook(Array(CommandLine.arguments.dropFirst(i+1)));return }
        if let i=CommandLine.arguments.firstIndex(of:"--relaunch-permission"),CommandLine.arguments.count>i+2 {
            exit(PermissionRelaunch.run(parent:CommandLine.arguments[i+1],resumeID:CommandLine.arguments[i+2]))
        }
        if CommandLine.arguments.contains("--permission-status") { print(Shortcut.permitted ? "granted" : "denied");return }
        if let i=CommandLine.arguments.firstIndex(of:"--radio-stream-tests"),i+1<CommandLine.arguments.count,let url=URL(string:CommandLine.arguments[i+1]) { exit(RadioTests.streaming(base:url)) }
        if CommandLine.arguments.contains("--self-test") { NativeTests.run();return }
        if CommandLine.arguments.contains("--ble-device-test") { exit(HardwareConnectionTests.runBLE()) }
        if CommandLine.arguments.contains("--quota-device-test") { exit(Engine.runQuotaDeviceTest(ble:CommandLine.arguments.contains("--ble-only"))) }
        let app=NSApplication.shared;let delegate=AppDelegate();app.delegate=delegate
        app.setActivationPolicy(.accessory);app.run();withExtendedLifetime(delegate){}
    }
}
