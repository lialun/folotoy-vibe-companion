import Foundation

/// Counts only empty output between two pieces of queued speech. Startup and
/// a drained tail are not midstream interruptions; PCM values are not inspected.
struct PCMContinuity {
    private var started=false,pending=0
    private(set) var interruptions=0,samples=0,longest=0

    mutating func consume(_ available:Int,requested:Int) {
        if available>0 {
            if started,pending>0 {interruptions+=1;samples+=pending;longest=max(longest,pending)}
            pending=0;started=true
        }
        if started {pending+=max(0,requested-available)}
    }
}
