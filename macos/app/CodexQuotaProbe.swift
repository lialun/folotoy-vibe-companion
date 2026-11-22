import Foundation
import Darwin

enum QuotaProbeError:Error {case unavailable,timeout,invalid,cancelled}

// One awake interval owns its workers. Register cancellation before launching,
// but never hold a lock across Process.run(), which can involve slow disk I/O.
final class QuotaProbeCancellation {
    private let lock=NSLock()
    private var cancelled=false,action:(()->Void)?
    var isCancelled:Bool { lock.lock();defer{lock.unlock()};return cancelled }
    func launch(_ process:Process,onCancel:@escaping ()->Void)throws {
        lock.lock()
        guard !cancelled else {lock.unlock();throw QuotaProbeError.cancelled}
        action=onCancel;lock.unlock()
        try process.run()
    }
    func cancel() {
        lock.lock();cancelled=true;let action=self.action;self.action=nil;lock.unlock()
        action?()
    }
}

// Owns only its read-only app-server child. Bounded output/lifetime; never starts a turn.
final class CodexQuotaProbe {
    private let process=Process(),input=Pipe(),output=Pipe()
    private let lock=NSLock(),ready=DispatchSemaphore(value:0)
    private var buffer=Data(),responses:[Int:[String:Any]]=[:],closed=false
    private let cancellation:QuotaProbeCancellation
    private init(executable:URL,cancellation:QuotaProbeCancellation)throws {
        self.cancellation=cancellation
        process.executableURL=executable;process.arguments=["app-server"]
        process.standardInput=input;process.standardOutput=output;process.standardError=FileHandle.nullDevice
        output.fileHandleForReading.readabilityHandler={ [weak self] handle in
            let bytes=handle.availableData;guard let self=self else{return}
            self.lock.lock();defer{self.lock.unlock()}
            if bytes.isEmpty {self.closed=true;self.ready.signal();return}
            if self.buffer.count+bytes.count>524288 {self.closed=true;self.ready.signal();return}
            self.buffer.append(bytes)
            while let end=self.buffer.firstIndex(of:10) {
                let line=Data(self.buffer[..<end]);self.buffer.removeSubrange(...end)
                if let o=(try? JSONSerialization.jsonObject(with:line)) as? [String:Any],let id=o["id"] as? Int,(1...4).contains(id) {self.responses[id]=o;self.ready.signal()}
            }
        }
        do { try cancellation.launch(process) { [weak self] in self?.cancel() } }
        catch {
            output.fileHandleForReading.readabilityHandler=nil
            try? input.fileHandleForWriting.close();try? output.fileHandleForReading.close()
            throw error
        }
    }
    private func cancel() {
        ready.signal()
        // The worker wakes immediately and owns process termination in close().
        // Do not race Process state from the notification/main thread.
    }
    private func write(_ object:[String:Any])throws {
        guard !cancellation.isCancelled else {throw QuotaProbeError.cancelled}
        var data=try JSONSerialization.data(withJSONObject:object);data.append(10);try input.fileHandleForWriting.write(contentsOf:data)
    }
    private func request(_ id:Int,_ method:String,_ params:[String:Any]?=nil)throws->[String:Any] {
        var object:[String:Any]=["id":id,"method":method];if let params=params{object["params"]=params};try write(object)
        let deadline=DispatchTime.now()+10
        while true {
            guard !cancellation.isCancelled else {throw QuotaProbeError.cancelled}
            lock.lock();let response=responses.removeValue(forKey:id),ended=closed;lock.unlock()
            if let response=response {guard let result=response["result"] as? [String:Any] else{throw QuotaProbeError.unavailable};return result}
            if ended{throw QuotaProbeError.unavailable}
            if ready.wait(timeout:deadline) == .timedOut {throw QuotaProbeError.timeout}
        }
    }
    private func close() {
        output.fileHandleForReading.readabilityHandler=nil
        try? input.fileHandleForWriting.close()
        if process.isRunning {process.terminate()}
        let end=Date().addingTimeInterval(1)
        while process.isRunning && Date()<end {Thread.sleep(forTimeInterval:0.01)}
        if process.isRunning {Darwin.kill(process.processIdentifier,SIGKILL)}
        process.waitUntilExit();try? output.fileHandleForReading.close()
    }
    static func read(executable:URL,cancellation:QuotaProbeCancellation=QuotaProbeCancellation())throws->AgentQuotaSample {
        let client=try CodexQuotaProbe(executable:executable,cancellation:cancellation);defer{client.close()}
        _=try client.request(1,"initialize",["clientInfo":["name":"vibe-companion-quota","version":"1"]])
        try client.write(["method":"initialized"])
        let before=try client.request(2,"account/read",["refreshToken":false])
        guard let account=before["account"] as? [String:Any],account["type"] as? String=="chatgpt" else{throw QuotaProbeError.unavailable}
        let result=try client.request(3,"account/rateLimits/read")
        let after=try client.request(4,"account/read",["refreshToken":false])
        guard let bytes=try? JSONSerialization.data(withJSONObject:["account":account,"userId":before["userId"] ?? NSNull(),"routing":before["workspaceRouting"] ?? NSNull()],options:.sortedKeys),
              let afterAccount=after["account"] as? [String:Any],NSDictionary(dictionary:account).isEqual(to:afterAccount),
              String(describing:before["userId"])==String(describing:after["userId"]),
              NSDictionary(dictionary:before["workspaceRouting"] as? [String:Any] ?? [:]).isEqual(to:after["workspaceRouting"] as? [String:Any] ?? [:]),
              let sample=AgentQuotaSample.codex(result,owner:AgentEvent.digest(bytes.base64EncodedString())) else{throw QuotaProbeError.invalid}
        return sample
    }
}
