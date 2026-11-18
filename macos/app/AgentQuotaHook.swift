import Foundation
import Darwin

enum AgentQuotaHook {
    static let flag="--vibe-quota-statusline"
    static var originalFile:URL {
        PairingStore.directory.appendingPathComponent("claude-statusline-"+AgentEvent.digest(AgentInstaller.config(.claude).path).prefix(16)+".json")
    }
    // Preserve the user's complete status-line object. Removal restores only our wrapper.
    static func configure(_ source:[String:Any],install:Bool,backup:URL=originalFile)throws->[String:Any] {
        var root=source
        let current=root["statusLine"] as? [String:Any]
        let ours=(current?["command"] as? String)?.contains(flag)==true
        if install {
            if ours{return root}
            if root["statusLine"] != nil && (current?["type"] as? String != "command" || current?["command"] as? String == nil) {return root}
            let value:[String:Any]=["present":root["statusLine"] != nil,"value":root["statusLine"] ?? NSNull()]
            try AgentInstaller.privateWrite(try JSONSerialization.data(withJSONObject:value),to:backup,backup:false)
            var wrapper=current ?? [:];wrapper["type"]="command"
            wrapper["command"]=AgentInstaller.quote(AgentInstaller.helper.path)+" "+flag
            root["statusLine"]=wrapper
        } else if ours {
            guard let data=try? Data(contentsOf:backup),let stored=try? JSONSerialization.jsonObject(with:data) as? [String:Any] else{return root}
            if stored["present"] as? Bool==true {root["statusLine"]=stored["value"]}
            else {root.removeValue(forKey:"statusLine")}
        }
        return root
    }
    static func run() {
        var data=Data(),buffer=[UInt8](repeating:0,count:8192)
        let deadline=Date().addingTimeInterval(0.8)
        while Date()<deadline && data.count<=262144 {
            var descriptor=pollfd(fd:STDIN_FILENO,events:Int16(POLLIN),revents:0)
            let ready=poll(&descriptor,1,30);if ready<0{break};if ready==0{continue}
            let count=Darwin.read(STDIN_FILENO,&buffer,buffer.count);if count<=0{break};data.append(contentsOf:buffer.prefix(count))
        }
        if data.count<=262144,let input=(try? JSONSerialization.jsonObject(with:data)) as? [String:Any],var sample=AgentQuotaSample.claude(input) {
            // Repeated status redraws retain their first-observed capture time,
            // even when the desktop app restarts. The file contains hashes/time only.
            if let captured=captureTime(id:sample.id,owner:sample.owner,now:sample.captured) {
                sample.captured=captured
                if sample.valid(at:Date()),let object=try? JSONSerialization.jsonObject(with:JSONEncoder().encode(sample)) {_=AgentReceiver.forwardMessage(["quota":object])}
            }
        }
        guard let bytes=try? Data(contentsOf:originalFile),let stored=(try? JSONSerialization.jsonObject(with:bytes)) as? [String:Any],
              let original=stored["value"] as? [String:Any],let command=original["command"] as? String,!command.contains(flag) else{return}
        let process=Process(),input=Pipe();process.executableURL=URL(fileURLWithPath:"/bin/sh");process.arguments=["-c",command]
        process.standardInput=input;process.standardOutput=FileHandle.standardOutput;process.standardError=FileHandle.nullDevice
        guard (try? process.run()) != nil else{return}
        DispatchQueue.global(qos:.utility).async {try? input.fileHandleForWriting.write(contentsOf:data);try? input.fileHandleForWriting.close()}
        let end=Date().addingTimeInterval(2)
        while process.isRunning && Date()<end{Thread.sleep(forTimeInterval:0.01)}
        if process.isRunning {process.terminate()}
    }
    static func captureTime(id:String,owner:String,now:Date,directory:URL=PairingStore.directory)->Date? {
        let file=directory.appendingPathComponent("quota-clock-"+owner.prefix(16)+".json")
        let lockFile=directory.appendingPathComponent("quota-clock.lock")
        let fd=Darwin.open(lockFile.path,O_RDWR|O_CREAT,0o600)
        guard fd>=0 else{return nil};defer{Darwin.close(fd)}
        guard flock(fd,LOCK_EX|LOCK_NB)==0 else{return nil};defer{flock(fd,LOCK_UN)}
        if let data=try? Data(contentsOf:file),let old=(try? JSONSerialization.jsonObject(with:data)) as? [String:Any],old["id"] as? String==id,let date=old["at"] as? Double {
            return Date(timeIntervalSince1970:date)
        }
        guard let data=try? JSONSerialization.data(withJSONObject:["id":id,"at":now.timeIntervalSince1970]) else{return nil}
        do {try AgentInstaller.privateWrite(data,to:file,backup:false);return now}catch{return nil}
    }
}
