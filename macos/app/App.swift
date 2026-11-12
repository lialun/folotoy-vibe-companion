import SwiftUI
import AppKit

final class AppDelegate:NSObject,NSApplicationDelegate,NSWindowDelegate {
    var model:AppModel?,window:NSWindow?,item:NSStatusItem?
    private var permissionReloaded=false,permissionProbeRunning=false,awaitingPermissionReturn=false
    func applicationDidFinishLaunching(_ notification:Notification) {
        guard !CommandLine.arguments.contains("--self-test") else { return }
        let preview=CommandLine.arguments.contains("--preview-ui") || Bundle.main.bundleIdentifier?.hasSuffix(".preview")==true
        var resumed:PermissionSession?
        if let i=CommandLine.arguments.firstIndex(of:"--resume-permission"),CommandLine.arguments.count>i+1 { resumed=PermissionSessionStore.consume(CommandLine.arguments[i+1]) }
        permissionReloaded=resumed != nil
        let peers=NSRunningApplication.runningApplications(withBundleIdentifier:Bundle.main.bundleIdentifier ?? "io.github.vibecompanion.mac")
        if !preview,resumed==nil,let existing=peers.first(where:{$0.processIdentifier != ProcessInfo.processInfo.processIdentifier}) { existing.activate(options:[.activateAllWindows]);NSApp.terminate(nil);return }
        let m=AppModel(preview:preview);model=m
        if let resumed=resumed { m.restore(resumed);m.result="快捷键授权已重新载入" }
        m.onPermissionRequested={ [weak self] in self?.permissionReloaded=false;self?.awaitingPermissionReturn=true }
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
    func applicationDidBecomeActive(_ notification:Notification) {
        refreshPermission()
        if let model=model,!model.preview { model.agents.refresh() }
    }
    func windowDidBecomeKey(_ notification:Notification) { refreshPermission() }
    private func refreshPermission() {
        guard let m=model,!m.preview,!permissionProbeRunning else { return }
        if Shortcut.permitted { permissionReloaded=false;awaitingPermissionReturn=false;m.engine.refreshReadiness();return }
        guard !permissionReloaded else { return } // Never enter an automatic restart loop.
        permissionProbeRunning=true;m.permissionChecking=true
        DispatchQueue.global(qos:.utility).async { [weak self] in
            let granted=PermissionProbe.check()
            DispatchQueue.main.async {
                guard let self=self,let m=self.model else { return }
                self.permissionProbeRunning=false;m.permissionChecking=false
                guard PermissionRecoveryPolicy.shouldReload(current:Shortcut.permitted,fresh:granted,returnedFromSettings:self.awaitingPermissionReturn,alreadyReloaded:self.permissionReloaded) else { m.engine.refreshReadiness();return }
                // Defer the reload until in-flight saves/probes/capture finish.
                if m.saving || m.radioLoading || m.radioChecking || m.state.recording {
                    DispatchQueue.main.asyncAfter(deadline:.now()+1) { [weak self] in self?.refreshPermission() };return
                }
                self.reloadAfterPermission()
            }
        }
    }
    private func reloadAfterPermission() {
        guard let m=model,let id=PermissionSessionStore.save(m.permissionSession(visible:awaitingPermissionReturn || window?.isVisible==true)) else {
            model?.result="授权已开启，但无法保留当前草稿；请保存后重新打开应用";return
        }
        permissionReloaded=true;awaitingPermissionReturn=false;m.permissionChecking=true;m.result="正在重新检测快捷键…"
        m.engine.stop { [weak self] in
            if PermissionRelaunch.schedule(resumeID:id) { NSApp.terminate(nil) }
            else {
                PermissionSessionStore.discard(id);self?.model?.permissionChecking=false
                self?.model?.result="无法重新载入快捷键权限，请稍后重试";self?.model?.engine.start()
            }
        }
    }

    func windowWillClose(_ notification:Notification) { NSApp.hide(nil) }
    @objc func closeWindow() { window?.performClose(nil) }
    @objc func show() { NSApp.unhide(nil);window?.makeKeyAndOrderFront(nil);NSApp.activate(ignoringOtherApps:true);refreshPermission() }
    @objc func quit() { NSApp.terminate(nil) }
    @objc func willSleep() { model?.engine.stop() }
    @objc func didWake() { if model?.preview != true { model?.engine.start() } }
    func applicationWillTerminate(_ notification:Notification) { model?.engine.stop() }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender:NSApplication)->Bool { false }
    func applicationShouldHandleReopen(_ sender:NSApplication,hasVisibleWindows:Bool)->Bool { show();return true }
}

@main struct VibeMain {
    static func main() {
        if let i=CommandLine.arguments.firstIndex(of:AgentInstaller.marker) { AgentReceiver.runHook(Array(CommandLine.arguments.dropFirst(i+1)));return }
        if let i=CommandLine.arguments.firstIndex(of:"--relaunch-permission"),CommandLine.arguments.count>i+2 {
            exit(PermissionRelaunch.run(parent:CommandLine.arguments[i+1],resumeID:CommandLine.arguments[i+2]))
        }
        if CommandLine.arguments.contains("--permission-status") { print(Shortcut.permitted ? "granted" : "denied");return }
        if CommandLine.arguments.contains("--self-test") { NativeTests.run();return }
        let app=NSApplication.shared;let delegate=AppDelegate();app.delegate=delegate
        app.setActivationPolicy(.accessory);app.run();withExtendedLifetime(delegate){}
    }
}
