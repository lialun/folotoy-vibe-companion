import Foundation

// NSWorkspace's system wake notification, not display state, resumes work.
// Display-only sleep/wake and background activity never transition this policy.
struct SleepResumePolicy {
    enum ResumeReason {case system,userInteraction,display,backgroundMaintenance}
    private(set) var waitingForWake=false
    private(set) var generation=0
    mutating func willSleep() {
        generation &+= 1;waitingForWake=true
    }
    func acceptsCompletion(from generation:Int)->Bool {
        !waitingForWake && self.generation==generation
    }
    mutating func resume(_ reason:ResumeReason)->Bool {
        guard waitingForWake,reason == .system || reason == .userInteraction else { return false }
        waitingForWake=false;return true
    }
    mutating func didWake()->Bool {resume(.system)}
    mutating func userRequestedResume()->Bool {resume(.userInteraction)}
}

// Short lock sections only; never run I/O or wait for a worker under this lock.
// A sleep closes admission immediately even if the network queue is busy.
final class EngineLifecycle {
    private let lock=NSLock()
    private var suspended=false,epoch=0,paused=false
    var isSuspended:Bool {lock.lock();defer{lock.unlock()};return suspended}
    var userPaused:Bool {
        get {lock.lock();defer{lock.unlock()};return paused}
        set {lock.lock();paused=newValue;lock.unlock()}
    }
    func suspend() {
        lock.lock();epoch &+= 1;suspended=true;lock.unlock()
    }
    func requestResume()->Int {
        lock.lock();defer{lock.unlock()};epoch &+= 1;return epoch
    }
    func completeResume(_ request:Int)->Bool {
        lock.lock();defer{lock.unlock()}
        guard epoch==request else {return false};suspended=false;return true
    }
}
