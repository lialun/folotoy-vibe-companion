import Foundation
import CoreFoundation

/// Explicit Enter intents have an identity, source timestamps, a recording
/// scope, and a deadline. A voice transition alone has nothing to dispatch.
final class InputCommands {
    private struct Intent {
        let id:UInt32,request:UInt32
        let expires:TimeInterval
    }
    private var timeline=DeviceEventTimeline()
    private var current:VoiceIdentity?
    private var lastRequest:UInt32?
    private var lastCommand:UInt32=0
    private var lastLegacyEvent=0
    private var blockedBefore:Double = -.infinity
    private var pending:[Intent]=[]
    var strict=false
    var postEnter:(()->Bool)?
    var log:((String)->Void)?
    var pendingCount:Int {pending.count}

    func resetScope() {
        timeline=DeviceEventTimeline();current=nil;lastRequest=nil
        lastCommand=0;lastLegacyEvent=0;pending=[];blockedBefore = -.infinity
    }

    func observe(milliseconds:Double,now:TimeInterval) {timeline.observe(milliseconds:milliseconds,now:now)}

    func voiceStarted(_ identity:VoiceIdentity) {
        current=identity;lastRequest=identity.request;pending=[]
    }

    func voiceFinished(_ result:VoiceSessionResult,now:TimeInterval) {
        guard current==result.identity else {return}
        current=nil
        let intents=pending
        pending=[]
        if !result.clean {fence(now:now);return}
        for intent in intents where intent.request==result.identity.request && now<=intent.expires {
            dispatch(intent.id)
        }
    }

    func cancel(now:TimeInterval) {
        current=nil;lastRequest=nil;pending=[];fence(now:now)
    }

    private func fence(now:TimeInterval) {
        if let boundary=timeline.cancellationBoundary(now:now) {blockedBefore=max(blockedBefore,boundary)}
    }

    func receive(_ body:[String:Any],now:TimeInterval) {
        guard body["action"] as? String=="enter" else {return}
        guard body["inputVersion"] as? Int==1 else {
            receiveLegacy(body,now:now);return
        }
        guard let id=VoiceControl.unsigned(body["command"]),id>lastCommand else {return}
        // Consume each identity once even if its contents are later rejected.
        lastCommand=id
        guard let request=VoiceControl.unsigned(body["voiceRequest"]),
              let pressed=body["pressedAtMs"] as? Double,let released=body["atMs"] as? Double,
              DeviceEventTimeline.valid(pressed),DeviceEventTimeline.valid(released),
              released>pressed,released-pressed<=VoicePolicy.maxCommandHold*1000 else {
            log?("Enter rejected command=\(id) reason=invalid-gesture");return
        }
        guard timeline.accepts(milliseconds:released) else {
            log?("Enter rejected command=\(id) reason=old-device-time");return
        }
        guard pressed>=blockedBefore else {
            log?("Enter rejected command=\(id) reason=before-cancel");return
        }
        if let current {
            guard current.request==request,pending.count<VoicePolicy.maxPendingCommands else {
                log?("Enter rejected command=\(id) reason=recording-scope");return
            }
            pending.append(Intent(id:id,request:request,expires:now+VoicePolicy.commandLifetime))
        } else {
            guard lastRequest==nil || lastRequest==request else {
                log?("Enter rejected command=\(id) reason=old-recording");return
            }
            dispatch(id)
        }
    }

    private func dispatch(_ id:UInt32) {
        if postEnter?()==true {log?("Enter posted command=\(id)")}
        else {log?("Enter rejected command=\(id) reason=keyboard-unavailable")}
    }

    private func receiveLegacy(_ body:[String:Any],now:TimeInterval) {
        // Old firmware has no command/session identity. Preserve an immediate
        // idle key, but never retain it for replay by a later voice stop.
        guard !strict,current==nil,let id=body["eid"] as? Int,id>lastLegacyEvent else {return}
        lastLegacyEvent=id
        if let at=body["atMs"] as? Double {
            guard timeline.accepts(milliseconds:at),at>=blockedBefore else {return}
        }
        if postEnter?()==true {log?("Enter posted legacy_eid=\(id)")}
    }

}
