import AppKit
import CoreBluetooth
import CryptoKit

/// The command-line runner reuses the invoking environment's existing access.
/// It never requests a new Bluetooth grant or posts real keyboard events.
@main enum VoiceBenchCLI {
    static func main() {
        if CommandLine.arguments==[CommandLine.arguments[0],"--self-test"] {NativeTests.run();return}
        if CommandLine.arguments==[CommandLine.arguments[0],"--permission-status"] {print(Shortcut.permitted ? "granted":"denied");return}
        if CommandLine.arguments==[CommandLine.arguments[0],"--permission-check"] {
            print("Bluetooth authorization: \(CBManager.authorization.rawValue)");return
        }
        guard CommandLine.arguments.count==3 else {fputs("Usage: VoiceWireBench plan.json results.json\n",stderr);exit(2)}
        do {
            let planURL=URL(fileURLWithPath:CommandLine.arguments[1])
            let destination=URL(fileURLWithPath:CommandLine.arguments[2])
            let plan=try VoiceBenchPlan.read(planURL)
            guard let executable=Bundle.main.executableURL else {throw VoiceBenchPlanError.invalid("Missing executable path")}
            let directory=executable.deletingLastPathComponent()
            try VoiceBenchIdentity.validatePaths(plan:planURL,output:destination,bundle:directory)
            let data=try Data(contentsOf:directory.appendingPathComponent("manifest.json"))
            let manifest=try JSONSerialization.jsonObject(with:data) as? [String:Any]
            let checksum=SHA256.hash(data:try Data(contentsOf:executable)).map{String(format:"%02x",$0)}.joined()
            guard manifest?["binarySha256"] as? String==checksum,
                  let digest=manifest?["sourceDigest"] as? String,
                  let version=manifest?["coreVersion"] as? String else {throw VoiceBenchPlanError.invalid("Backend build identity mismatch; rebuild with tools/voice_bench.py build-backend")}
            guard [VoiceBenchIdentity.productionBundle,VoiceBenchIdentity.bundle].allSatisfy({NSRunningApplication.runningApplications(withBundleIdentifier:$0).isEmpty}) else {throw VoiceBenchPlanError.invalid("Close the normal Vibe app and graphical Voice Bench before starting")}
            if plan.transport != .wifi,CBManager.authorization != .allowedAlways {
                fputs("Existing Bluetooth access is unavailable; no permission request was made\n",stderr);exit(77)
            }
            let saved=UserDefaults.standard.persistentDomain(forName:VoiceBenchIdentity.productionBundle) ?? [:]
            let snapshot=try VoiceBenchIdentity.connectionSnapshot(saved)
            guard let device=snapshot["device"],PairingStore.load(device) != nil else {throw VoiceBenchPlanError.invalid("Existing device credential unavailable")}
            var inputs=snapshot;inputs["connectionTransport"]=plan.transport == .ble ? "bluetooth":"wifi"
            UserDefaults.standard.setVolatileDomain(inputs,forName:UserDefaults.argumentDomain)
            try FileManager.default.createDirectory(at:destination.deletingLastPathComponent(),withIntermediateDirectories:true)
            let sequenceURL=destination.deletingLastPathComponent().appendingPathComponent("voice-bench-sequence.json")
            let previous=(try? Data(contentsOf:sequenceURL)).flatMap{try? JSONDecoder().decode(Int.self,from:$0)} ?? 0
            let gui=UserDefaults.standard.persistentDomain(forName:VoiceBenchIdentity.bundle)?["benchSequence"] as? Int ?? 0
            let last=max(previous,gui)
            guard last>=0,last<Int(UInt32.max)-4096 else {throw VoiceBenchPlanError.invalid("Test sequence exhausted")}
            let run=VoiceBenchRun(plan:plan,destination:destination,lastSequence:last,sourceVersion:version,sourceDigest:digest)
            run.onSequence={ value in if let data=try? JSONEncoder().encode(value) {try? data.write(to:sequenceURL,options:.atomic)} }
            run.onProgress={print($0);fflush(stdout)}
            let keyboard=Shortcut(permissionCheck:{true},eventSink:{code,down,_ in if down && code==36 {VoiceBenchRun.enterCount+=1}})
            let engine=Engine(lanEnabled:plan.transport != .ble,bleEnabled:plan.transport != .wifi,keyboard:keyboard)
            run.onFinished={engine.stop {
                let result=(try? Data(contentsOf:destination)).flatMap{try? JSONSerialization.jsonObject(with:$0)} as? [String:Any]
                exit(result?["pass"] as? Bool==true ? 0:1)
            }}
            signal(SIGINT,SIG_IGN)
            let interrupt=DispatchSource.makeSignalSource(signal:SIGINT,queue:.main)
            interrupt.setEventHandler {engine.stop {exit(130)}};interrupt.resume()
            let deadline=DispatchSource.makeTimerSource(queue:.main)
            deadline.schedule(deadline:.now()+plan.observationLimit+10)
            deadline.setEventHandler {fputs("Backend observation deadline\n",stderr);exit(124)};deadline.resume()
            let sleep=NSWorkspace.shared.notificationCenter.addObserver(forName:NSWorkspace.willSleepNotification,object:nil,queue:nil) { _ in
                engine.suspendForSleep()
            }
            engine.attachVoiceBench(run);engine.start()
            withExtendedLifetime((engine,run,interrupt,deadline,sleep)) {dispatchMain()}
        } catch {fputs("\(error.localizedDescription)\n",stderr);exit(2)}
    }
}
