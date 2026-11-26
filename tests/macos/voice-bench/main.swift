import Foundation

var checks=0
func check(_ condition:Bool,_ message:String) {
    checks+=1
    if !condition {fputs("FAIL: \(message)\n",stderr);exit(1)}
}
func decode(_ body:[String:Any]) throws->VoiceBenchPlan {
    try JSONDecoder().decode(VoiceBenchPlan.self,from:JSONSerialization.data(withJSONObject:body))
}
let stage:[String:Any]=["name":"baseline","mask":3,"seconds":10]
let base:[String:Any]=["schema":1,"transport":"dual","stages":[stage]]
let plan=try decode(base)
check(VoiceBenchPlanError.invalid("Detailed reason").localizedDescription=="Detailed reason","setup failures preserve an actionable message")
check(plan.stages.count==1 && plan.observationLimit==135,"bounded plan with defaults")
try VoiceBenchIdentity.validateLaunchArguments(["--plan","/tmp/plan.json","--output","/tmp/result.json"])
for arguments in [["--notify"],["--agent-hook","codex"],["--plan"],["--plan","a","--plan","b"],["--output","--plan"]] {
    check((try? VoiceBenchIdentity.validateLaunchArguments(arguments))==nil,"unknown/helper arguments never open a GUI")
}
func rejected(_ body:[String:Any],_ message:String) {
    do {_=try decode(body);check(false,message)} catch {check(true,message)}
}
for (key,value) in [("schema",2 as Any),("transport","invalid" as Any),("connectionTimeout",9 as Any),("connectionTimeout",181 as Any),("typo",true as Any),("stages",[] as Any)] {
    var candidate=base;candidate[key]=value;rejected(candidate,"reject invalid \(key)")
}
for (key,value) in [("seconds",0 as Any),("seconds",181 as Any),("mask",4 as Any),("fault",5 as Any),("controls","yes" as Any),("unknown",1 as Any),("name","bad name" as Any)] {
    var item=stage;item[key]=value;var candidate=base;candidate["stages"]=[item];rejected(candidate,"reject invalid stage \(key)")
}
var duplicate=base;duplicate["stages"]=[stage,stage];rejected(duplicate,"duplicate identifiers")
var radioStage=stage;radioStage["radio"]=true
var radioPlan=base;radioPlan["stages"]=[radioStage]
rejected(radioPlan,"radio stage requires an explicit test stream")
radioPlan["radioURL"]="http://127.0.0.1:18180/tone.mp3"
check((try? decode(radioPlan)) != nil,"local radio fixture stream")
radioPlan["radioURL"]="https://user:password@example.org/tone.mp3"
rejected(radioPlan,"credentials never enter radio plans")
for mode in ["wifi","ble"] {
    var candidate=base;candidate["transport"]=mode;rejected(candidate,"mode rejects unavailable carrier")
    var item=stage;item["mask"]=mode=="wifi" ? 1:2;candidate["stages"]=[item]
    check((try? decode(candidate)) != nil,"single carrier accepted")
    item["changes"]=[["at":1,"mask":3]];candidate["stages"]=[item]
    rejected(candidate,"changed mask rejects unavailable carrier")
}
for changes in [[["at":2,"mask":0]],[["at":2,"mask":1],["at":1,"mask":2]],[["at":10,"mask":1]],[["at":1,"mask":1,"typo":1]]] {
    var item=stage;item["changes"]=changes;var candidate=base;candidate["stages"]=[item];rejected(candidate,"unsafe mask change")
}
var outage=stage;outage["outage"]=true;var candidate=base;candidate["stages"]=[outage]
rejected(candidate,"outage requires interruption")
outage["changes"]=[["at":1,"mask":0],["at":7,"mask":3]];candidate["stages"]=[outage]
check((try? decode(candidate)) != nil,"explicit outage accepted")
candidate=base;candidate["stages"]=(0..<64).map{["name":"stage\($0)","mask":3,"seconds":180]}
rejected(candidate,"whole plan observation bound")
let snapshot=try VoiceBenchIdentity.connectionSnapshot(["device":"012345abcdef","computerIdentity":UUID().uuidString,"shortcut":"ctrl+option+v","launchAtLogin":true,"agentSettings":["secret":"excluded"],"token":"excluded"])
check(Set(snapshot.keys)==["device","computerIdentity","shortcut"],"copy only connection inputs, no agent/login/credential settings")
check((try? VoiceBenchIdentity.connectionSnapshot([:]))==nil,"no accidental new owner identity")
let folder=FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
try FileManager.default.createDirectory(at:folder,withIntermediateDirectories:true)
defer {try? FileManager.default.removeItem(at:folder)}
let bundle=folder.appendingPathComponent("Bench.app"),input=folder.appendingPathComponent("plan.json"),output=folder.appendingPathComponent("results.json")
try VoiceBenchIdentity.validatePaths(plan:input,output:output,bundle:bundle)
check((try? VoiceBenchIdentity.validatePaths(plan:input,output:input,bundle:bundle))==nil,"cannot overwrite plan")
check((try? VoiceBenchIdentity.validatePaths(plan:input,output:bundle.appendingPathComponent("results.json"),bundle:bundle))==nil,"cannot write inside signed bundle")
try Data("{}".utf8).write(to:input)
let alias=folder.appendingPathComponent("alias.json")
try FileManager.default.createSymbolicLink(at:alias,withDestinationURL:input)
check((try? VoiceBenchIdentity.validatePaths(plan:input,output:alias,bundle:bundle))==nil,"cannot overwrite plan through symlink")
try Data(repeating:32,count:65537).write(to:input)
check((try? VoiceBenchPlan.read(input))==nil,"oversized plan rejected")
print("Voice bench plan/isolation: \(checks) checks passed")
