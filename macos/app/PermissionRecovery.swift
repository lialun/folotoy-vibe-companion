import Foundation
import CoreGraphics
import AppKit
import Darwin

// A fresh invocation of this same signed executable checks PostEvent only. It
// never asks for permission, posts a key, reads TCC, or substitutes an AX grant.
enum PermissionProbe {
    static func check(executable:URL?=Bundle.main.executableURL)->Bool? {
        guard let executable=executable else { return nil }
        let process=Process(),pipe=Pipe(),finished=DispatchSemaphore(value:0)
        process.executableURL=executable;process.arguments=["--permission-status"]
        process.standardOutput=pipe;process.standardError=FileHandle.nullDevice
        process.terminationHandler={ _ in finished.signal() }
        do { try process.run();pipe.fileHandleForWriting.closeFile() } catch { return nil }
        guard finished.wait(timeout:.now()+3) == .success else { process.terminate();return nil }
        guard process.terminationStatus==0 else { return nil }
        let reply=String(data:pipe.fileHandleForReading.readDataToEndOfFile(),encoding:.utf8)?.trimmingCharacters(in:.whitespacesAndNewlines)
        return reply=="granted" ? true : reply=="denied" ? false : nil
    }
}

struct PermissionSession:Codable {
    var created=Date()
    var settings:DeviceSettings,confirmed:DeviceSettings?
    var loaded:Bool,shortcut:String,cityQuery:String
    var stations:[RadioStation],baseline:[RadioStation]
    var revision:Int,capacity:Int,radioLoaded:Bool
    var query:String,chineseOnly:Bool,source:Int,customName:String,customURL:String
    var tab:Int,visible:Bool
}

enum PermissionSessionStore {
    static func save(_ session:PermissionSession,directory:URL=PairingStore.directory)->String? {
        let id=UUID().uuidString,file=directory.appendingPathComponent("permission-resume-\(id).json")
        do {
            try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true,attributes:[.posixPermissions:0o700])
            try FileManager.default.setAttributes([.posixPermissions:0o700],ofItemAtPath:directory.path)
            // Create privately before writing; atomic Data.write can replace a
            // private file with a new file using the process's broader umask.
            let data=try JSONEncoder().encode(session)
            guard FileManager.default.createFile(atPath:file.path,contents:data,attributes:[.posixPermissions:0o600]) else { return nil }
            return id
        } catch { return nil }
    }
    static func consume(_ id:String,directory:URL=PairingStore.directory)->PermissionSession? {
        guard UUID(uuidString:id) != nil else { return nil }
        let file=directory.appendingPathComponent("permission-resume-\(id).json")
        guard let data=try? Data(contentsOf:file) else { return nil }
        try? FileManager.default.removeItem(at:file)
        guard data.count<=262144,let session=try? JSONDecoder().decode(PermissionSession.self,from:data),
              abs(session.created.timeIntervalSinceNow)<600 else { return nil }
        return session
    }
    static func discard(_ id:String) { _=consume(id) }
}

// Relaunch once after returning from an explicitly requested system grant even
// when TCC also caches denial for a child process. This never grants access:
// the replacement engine still requires a successful PostEvent preflight.
enum PermissionRecoveryPolicy {
    static func shouldReload(current:Bool,fresh:Bool?,returnedFromSettings:Bool,alreadyReloaded:Bool)->Bool {
        !current && !alreadyReloaded && (fresh==true || returnedFromSettings)
    }
}

// Start only after the old GUI process has exited, so Launch Services does not
// confuse two instances with the same bundle identifier during registration.
enum PermissionRelaunch {
    static func schedule(resumeID:String)->Bool {
        guard let executable=Bundle.main.executableURL else { return false }
        let process=Process();process.executableURL=executable
        process.arguments=["--relaunch-permission",String(ProcessInfo.processInfo.processIdentifier),resumeID]
        process.standardInput=FileHandle.nullDevice;process.standardOutput=FileHandle.nullDevice;process.standardError=FileHandle.nullDevice
        do { try process.run();return true } catch { return false }
    }
    static func run(parent:String,resumeID:String)->Int32 {
        guard let pid=Int32(parent),pid>1,pid != getpid(),UUID(uuidString:resumeID) != nil else { return 2 }
        let deadline=Date().addingTimeInterval(15)
        while Darwin.kill(pid,0)==0 || errno==EPERM {
            guard Date()<deadline else { return 3 }
            Thread.sleep(forTimeInterval:0.1)
        }
        let configuration=NSWorkspace.OpenConfiguration();configuration.arguments=["--resume-permission",resumeID]
        var finished=false,status:Int32=4
        NSWorkspace.shared.openApplication(at:Bundle.main.bundleURL,configuration:configuration) { _,error in
            status=error==nil ? 0 : 4;finished=true
        }
        let launchDeadline=Date().addingTimeInterval(15)
        while !finished && Date()<launchDeadline { RunLoop.current.run(until:Date().addingTimeInterval(0.1)) }
        return finished ? status : 4
    }
}
