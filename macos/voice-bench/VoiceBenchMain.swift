import AppKit
import CoreBluetooth

final class VoiceBenchApp:NSObject,NSApplicationDelegate,NSWindowDelegate {
    private var window:NSWindow?,engine:Engine?,run:VoiceBenchRun?
    private let status=NSTextField(wrappingLabelWithString:"Load a plan, then start when the device is idle.")
    private let start=NSButton(title:"Start",target:nil,action:nil)
    private var stopping=false
    func applicationDidFinishLaunching(_ notification:Notification) {
        let menu=NSMenu(),app=NSMenuItem();app.submenu=NSMenu()
        app.submenu?.addItem(withTitle:"Quit Voice Bench",action:#selector(NSApplication.terminate(_:)),keyEquivalent:"q")
        menu.addItem(app);NSApp.mainMenu=menu
        let window=NSWindow(contentRect:NSRect(x:0,y:0,width:600,height:240),styleMask:[.titled,.closable,.miniaturizable],backing:.buffered,defer:false)
        window.title=VoiceBenchIdentity.name;window.delegate=self;window.isReleasedWhenClosed=false
        let note=NSTextField(wrappingLabelWithString:"Uses the production voice engine with simulated keyboard input. Requires authorized test firmware. Close the normal Vibe app before starting.")
        start.target=self;start.action=#selector(begin)
        let layout=NSStackView(views:[note,status,start]);layout.orientation = .vertical;layout.spacing=20;layout.edgeInsets=NSEdgeInsets(top:24,left:24,bottom:24,right:24)
        layout.translatesAutoresizingMaskIntoConstraints=false;window.contentView?.addSubview(layout)
        if let view=window.contentView {NSLayoutConstraint.activate([layout.leadingAnchor.constraint(equalTo:view.leadingAnchor),layout.trailingAnchor.constraint(equalTo:view.trailingAnchor),layout.topAnchor.constraint(equalTo:view.topAnchor),layout.bottomAnchor.constraint(lessThanOrEqualTo:view.bottomAnchor)])}
        window.center();window.makeKeyAndOrderFront(nil);self.window=window;NSApp.activate(ignoringOtherApps:true)
    }
    private func argument(_ flag:String)->String? {
        guard let i=CommandLine.arguments.firstIndex(of:flag),i+1<CommandLine.arguments.count else{return nil}
        return CommandLine.arguments[i+1]
    }
    @objc private func begin() {
        guard engine==nil else{return}
        do {
            guard Bundle.main.bundleIdentifier==VoiceBenchIdentity.bundle else {throw VoiceBenchPlanError.invalid("Wrong application identity; refusing to start")}
            guard NSRunningApplication.runningApplications(withBundleIdentifier:VoiceBenchIdentity.productionBundle).isEmpty else {throw VoiceBenchPlanError.invalid("Close the normal Vibe app before starting")}
            let files=FileManager.default,base=VoiceBenchIdentity.directory
            try files.createDirectory(at:base,withIntermediateDirectories:true,attributes:[.posixPermissions:0o700])
            let planURL=URL(fileURLWithPath:argument("--plan") ?? base.appendingPathComponent("plan.json").path)
            let output=URL(fileURLWithPath:argument("--output") ?? base.appendingPathComponent("results.json").path)
            try VoiceBenchIdentity.validatePaths(plan:planURL,output:output,bundle:Bundle.main.bundleURL)
            let plan=try VoiceBenchPlan.read(planURL)
            let source=UserDefaults.standard.persistentDomain(forName:VoiceBenchIdentity.productionBundle) ?? [:]
            let snapshot=try VoiceBenchIdentity.connectionSnapshot(source)
            guard let device=snapshot["device"],PairingStore.load(device) != nil else {throw VoiceBenchPlanError.invalid("No existing device credential; pair in the normal app first")}
            for (key,value) in snapshot {UserDefaults.standard.set(value,forKey:key)}
            let last=UserDefaults.standard.integer(forKey:"benchSequence")
            guard last>=0,last<Int(UInt32.max)-4096 else {throw VoiceBenchPlanError.invalid("Test sequence exhausted")}
            let run=VoiceBenchRun(plan:plan,destination:output,lastSequence:last)
            run.onSequence={UserDefaults.standard.set($0,forKey:"benchSequence")}
            run.onProgress={ [weak self] message in DispatchQueue.main.async {self?.status.stringValue=message} }
            run.onFinished={ [weak self] in DispatchQueue.main.async {self?.finish()} }
            let keyboard=Shortcut(permissionCheck:{true},eventSink:{code,down,_ in if down && code==36 {VoiceBenchRun.enterCount+=1}})
            let engine=Engine(lanEnabled:plan.transport != .ble,bleEnabled:plan.transport != .wifi,keyboard:keyboard)
            self.run=run;self.engine=engine;stopping=false;start.isEnabled=false;status.stringValue="Connecting…"
            engine.attachVoiceBench(run);engine.start()
        } catch {status.stringValue=error.localizedDescription}
    }
    private func finish() {
        guard !stopping,let engine else{return};stopping=true
        engine.stop { [weak self] in self?.engine=nil;self?.run=nil;self?.start.isEnabled=true;self?.stopping=false }
    }
    func applicationWillTerminate(_ notification:Notification) {stopping=true;engine?.stop()}
    func applicationShouldTerminateAfterLastWindowClosed(_ sender:NSApplication)->Bool {true}
}

@main enum VoiceBenchMain {
    static func main() {
        if CommandLine.arguments.contains("--permission-status") {print(Shortcut.permitted ? "granted":"denied");return}
        if CommandLine.arguments==[CommandLine.arguments[0],"--self-test"] {NativeTests.run();return}
        if let i=CommandLine.arguments.firstIndex(of:"--check-plan"),i+1<CommandLine.arguments.count {
            do {let plan=try VoiceBenchPlan.read(URL(fileURLWithPath:CommandLine.arguments[i+1]));print("Plan valid: \(plan.stages.count) stages; bound \(Int(plan.observationLimit)) seconds")}
            catch {fputs("\(error.localizedDescription)\n",stderr);exit(2)}
            return
        }
        do {try VoiceBenchIdentity.validateLaunchArguments(Array(CommandLine.arguments.dropFirst()))}
        catch {fputs("\(error.localizedDescription)\n",stderr);exit(2)}
        guard Bundle.main.bundleIdentifier==VoiceBenchIdentity.bundle else {fputs("Invalid bench application identity\n",stderr);exit(2)}
        if let other=NSRunningApplication.runningApplications(withBundleIdentifier:VoiceBenchIdentity.bundle).first(where:{$0.processIdentifier != getpid()}) {
            other.activate(options:[.activateAllWindows]);return
        }
        let app=NSApplication.shared,delegate=VoiceBenchApp();app.delegate=delegate
        app.setActivationPolicy(.regular);app.run();withExtendedLifetime(delegate){}
    }
}
