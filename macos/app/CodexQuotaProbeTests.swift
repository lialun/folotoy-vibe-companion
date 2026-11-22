import Foundation
import Darwin

private final class SlowQuotaLaunch:Process,@unchecked Sendable {
    let entered=DispatchSemaphore(value:0),release=DispatchSemaphore(value:0)
    override func run()throws {entered.signal();_=release.wait(timeout:.now()+5)}
}

enum CodexQuotaProbeTests {
    static func run(_ check:(Bool,String)->Void) {
        let cancelled=QuotaProbeCancellation();cancelled.cancel()
        let slow=SlowQuotaLaunch(),launchCancellation=QuotaProbeCancellation(),done=DispatchSemaphore(value:0),notified=DispatchSemaphore(value:0)
        DispatchQueue.global(qos:.utility).async {
            try? launchCancellation.launch(slow) {notified.signal()};done.signal()
        }
        check(slow.entered.wait(timeout:.now()+5) == .success,"simulate a slow subprocess launch")
        let before=ProcessInfo.processInfo.systemUptime
        launchCancellation.cancel()
        check(ProcessInfo.processInfo.systemUptime-before<0.5 && notified.wait(timeout:.now()) == .success,"sleep cancellation does not wait on slow process launch")
        slow.release.signal()
        check(done.wait(timeout:.now()+5) == .success,"slow launch worker can finish after cancellation")
        do {
            _=try CodexQuotaProbe.read(executable:URL(fileURLWithPath:"/missing-vibe-quota-probe"),cancellation:cancelled)
            check(false,"cancelled quota work must not launch")
        } catch QuotaProbeError.cancelled {
            check(true,"sleep before worker launch prevents spawning the quota subprocess")
        } catch { check(false,"cancelled quota work tried to launch an executable") }

        let directory=FileManager.default.temporaryDirectory.appendingPathComponent("vibe-quota-sleep-"+UUID().uuidString)
        do {
            try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true)
            defer {try? FileManager.default.removeItem(at:directory)}
            let executable=directory.appendingPathComponent("stalled-agent")
            // An owned child accepts input but never replies. No Agent install,
            // account, network or model request is involved.
            try Data("#!/bin/sh\nprintf '%s' \"$$\" > \"$0.pid\"\nwhile IFS= read -r line; do :; done\n".utf8).write(to:executable)
            try FileManager.default.setAttributes([.posixPermissions:0o700],ofItemAtPath:executable.path)
            let cancellation=QuotaProbeCancellation(),finished=DispatchSemaphore(value:0)
            DispatchQueue.global(qos:.utility).async {
                _=try? CodexQuotaProbe.read(executable:executable,cancellation:cancellation)
                finished.signal()
            }
            let marker=URL(fileURLWithPath:executable.path+".pid"),deadline=Date().addingTimeInterval(5)
            var pid:Int32?
            while pid==nil,Date()<deadline {
                pid=(try? String(contentsOf:marker,encoding:.utf8)).flatMap(Int32.init)
                if pid==nil {Thread.sleep(forTimeInterval:0.01)}
            }
            cancellation.cancel();cancellation.cancel()
            let stopped=finished.wait(timeout:.now()+5) == .success
            check(pid != nil && stopped,"sleep interrupts a stalled quota response without its ten-second timeout")
            if let pid=pid,stopped {check(Darwin.kill(pid,0)<0 && errno==ESRCH,"quota cancellation reaps its owned subprocess")}
        } catch {check(false,"quota sleep fixture could not be prepared")}
    }
}
