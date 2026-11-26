import Foundation

enum VoiceBenchIdentity {
    static let bundle="io.github.vibecompanion.voicebench"
    static let productionBundle="io.github.vibecompanion.mac"
    static let name="Vibe Voice Bench"
    static var directory:URL {
        FileManager.default.urls(for:.applicationSupportDirectory,in:.userDomainMask)[0]
            .appendingPathComponent(bundle,isDirectory:true)
    }
    static func validateLaunchArguments(_ arguments:[String]) throws {
        guard arguments.count%2==0 else {throw VoiceBenchPlanError.invalid("Each launch option needs a path")}
        var seen=Set<String>()
        for index in stride(from:0,to:arguments.count,by:2) {
            let option=arguments[index],value=arguments[index+1]
            guard ["--plan","--output"].contains(option),seen.insert(option).inserted,!value.isEmpty,!value.hasPrefix("--") else {
                throw VoiceBenchPlanError.invalid("Unsupported or duplicate launch option")
            }
        }
    }
    static func validatePaths(plan:URL,output:URL,bundle:URL) throws {
        let base=bundle.resolvingSymlinksInPath().standardizedFileURL.path
        let input=plan.resolvingSymlinksInPath().standardizedFileURL.path
        let result=output.resolvingSymlinksInPath().standardizedFileURL.path
        guard input != result,input != base,result != base,
              !input.hasPrefix(base+"/"),!result.hasPrefix(base+"/") else {
            throw VoiceBenchPlanError.invalid("Plans/results must stay outside the signed app and use different files")
        }
    }
    static func connectionSnapshot(_ source:[String:Any]) throws->[String:String] {
        guard let device=source["device"] as? String,device.count==12,
              device.allSatisfy({"0123456789abcdef".contains($0)}),
              let identity=source["computerIdentity"] as? String,UUID(uuidString:identity) != nil else {
            throw VoiceBenchPlanError.invalid("Pair this device in the normal app first")
        }
        let shortcut=source["shortcut"] as? String ?? "ctrl+option+v"
        guard shortcut.utf8.count<=160 else {throw VoiceBenchPlanError.invalid("Invalid saved shortcut")}
        // Only these connection inputs are copied into the bench's own domain.
        // Login items, agent settings and production preferences stay separate.
        return ["device":device,"computerIdentity":identity.lowercased(),"shortcut":shortcut]
    }
}

enum VoiceBenchPlanError:Error,LocalizedError {
    case invalid(String)
    var errorDescription:String? {switch self {case .invalid(let message):return message}}
}

private struct BenchKey:CodingKey {
    var stringValue:String
    var intValue:Int? {nil}
    init?(stringValue:String) {self.stringValue=stringValue}
    init?(intValue:Int) {return nil}
}
private func benchKeys(_ decoder:Decoder,_ allowed:Set<String>) throws {
    let keys=try decoder.container(keyedBy:BenchKey.self).allKeys.map{$0.stringValue}
    if let unknown=keys.first(where:{!allowed.contains($0)}) {
        throw VoiceBenchPlanError.invalid("Unknown plan field: \(unknown)")
    }
}

struct VoiceBenchPlan:Decodable {
    enum Transport:String,Decodable {case dual,wifi,ble}
    struct Change:Decodable {
        let at:Double,mask:Int
        private enum CodingKeys:String,CodingKey {case at,mask}
        init(from decoder:Decoder) throws {
            try benchKeys(decoder,["at","mask"])
            let c=try decoder.container(keyedBy:CodingKeys.self)
            at=try c.decode(Double.self,forKey:.at);mask=try c.decode(Int.self,forKey:.mask)
        }
    }
    struct Stage:Decodable {
        let name:String,mask:Int,seconds:Double,fault:Int
        let changes:[Change],controls:Bool,hostPause:Bool,outage:Bool,radio:Bool
        private enum CodingKeys:String,CodingKey {case name,mask,seconds,fault,changes,controls,hostPause,outage,radio}
        init(from decoder:Decoder) throws {
            try benchKeys(decoder,["name","mask","seconds","fault","changes","controls","hostPause","outage","radio"])
            let c=try decoder.container(keyedBy:CodingKeys.self)
            name=try c.decode(String.self,forKey:.name);mask=try c.decode(Int.self,forKey:.mask)
            seconds=try c.decode(Double.self,forKey:.seconds);fault=try c.decodeIfPresent(Int.self,forKey:.fault) ?? 0
            changes=try c.decodeIfPresent([Change].self,forKey:.changes) ?? []
            controls=try c.decodeIfPresent(Bool.self,forKey:.controls) ?? false
            hostPause=try c.decodeIfPresent(Bool.self,forKey:.hostPause) ?? false
            outage=try c.decodeIfPresent(Bool.self,forKey:.outage) ?? false
            radio=try c.decodeIfPresent(Bool.self,forKey:.radio) ?? false
        }
    }
    let schema:Int,transport:Transport,connectionTimeout:Double,stages:[Stage]
    let radioURL:String?
    private enum CodingKeys:String,CodingKey {case schema,transport,connectionTimeout,stages,radioURL}
    init(from decoder:Decoder) throws {
        try benchKeys(decoder,["schema","transport","connectionTimeout","stages","radioURL"])
        let c=try decoder.container(keyedBy:CodingKeys.self)
        schema=try c.decode(Int.self,forKey:.schema);transport=try c.decode(Transport.self,forKey:.transport)
        connectionTimeout=try c.decodeIfPresent(Double.self,forKey:.connectionTimeout) ?? 90
        stages=try c.decode([Stage].self,forKey:.stages)
        radioURL=try c.decodeIfPresent(String.self,forKey:.radioURL)
        try validate()
    }
    var observationLimit:Double {connectionTimeout+stages.reduce(0){$0+$1.seconds+35}}
    private func validate() throws {
        func require(_ condition:Bool,_ message:String) throws {
            if !condition {throw VoiceBenchPlanError.invalid(message)}
        }
        try require(schema==1,"Unsupported plan schema")
        try require(connectionTimeout.isFinite && (10...180).contains(connectionTimeout),"Connection timeout must be 10...180 seconds")
        try require((1...64).contains(stages.count),"A plan needs 1...64 stages")
        if stages.contains(where:{$0.radio}) {
            guard let radioURL,radioURL.utf8.count<=256,let url=URL(string:radioURL),
                  ["http","https"].contains(url.scheme ?? ""),url.host != nil,url.user==nil,url.password==nil else {
                throw VoiceBenchPlanError.invalid("Radio stages need a valid credential-free radioURL")
            }
        }
        var names=Set<String>()
        for stage in stages {
            let validName=stage.name.range(of:"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$",options:.regularExpression) != nil
            try require(validName && names.insert(stage.name).inserted,"Stage names must be unique short identifiers")
            try require(stage.seconds.isFinite && (0.5...180).contains(stage.seconds),"Stage duration must be 0.5...180 seconds")
            try require((0...3).contains(stage.mask) && (0...4).contains(stage.fault),"Unsupported media mask or fault")
            try require(transport != .wifi || stage.mask&2==0,"A Wi-Fi-only app cannot submit BLE media")
            try require(transport != .ble || stage.mask&1==0,"A BLE-only app cannot submit Wi-Fi media")
            try require(stage.changes.count<=16,"Too many mask changes")
            var previous = -Double.infinity
            for change in stage.changes {
                try require(change.at.isFinite && change.at>=0 && change.at<stage.seconds && change.at>previous,"Mask changes must be ordered within the stage")
                try require((0...3).contains(change.mask),"Unsupported changed mask")
                try require(transport != .wifi || change.mask&2==0,"Changed mask requires unavailable BLE")
                try require(transport != .ble || change.mask&1==0,"Changed mask requires unavailable Wi-Fi")
                previous=change.at
            }
            try require(stage.outage || (stage.mask != 0 && !stage.changes.contains{$0.mask==0}),"A zero media mask must declare expected outage")
            try require(!stage.outage || stage.changes.contains{$0.mask==0},"An outage stage must actually interrupt media")
        }
        try require(observationLimit<=2700,"Plan exceeds the 45-minute observation bound")
    }
    static func read(_ url:URL) throws->VoiceBenchPlan {
        let data=try Data(contentsOf:url)
        guard data.count<=65536 else{throw VoiceBenchPlanError.invalid("Plan exceeds 64 KiB")}
        return try JSONDecoder().decode(Self.self,from:data)
    }
}
