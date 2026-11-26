import Foundation

/// Submission describes the complete request, not admission to a local queue.
/// An ambiguous disconnect is charged conservatively; only a request known
/// not to have reached either transport is free to retry.
enum VoiceRepairSubmission:Equatable {
    case submitted,notSubmitted,uncertain
}
typealias VoiceRepairCompletion=(VoiceRepairSubmission,TimeInterval)->Void

struct VoiceRepairReceipt {
    private var pending:UInt8
    private var uncertain=false,finished=false
    init(wifi:Bool,bluetooth:Bool) {pending=(wifi ? 1:0)|(bluetooth ? 2:0)}

    mutating func complete(_ leg:ConnectionTransport,_ result:VoiceRepairSubmission)->VoiceRepairSubmission? {
        let bit:UInt8=leg == .wifi ? 1:2
        guard !finished,pending&bit != 0 else {return nil}
        pending &= ~bit
        if result == .submitted {finished=true;return .submitted}
        uncertain = uncertain || result == .uncertain
        guard pending==0 else {return nil}
        finished=true
        return uncertain ? .uncertain:.notSubmitted
    }
}

struct VoiceRepairRequest:Equatable {
    let id:UInt32,frames:[UInt32]
}

/// One bounded reservation at a time. Retry age and per-frame attempts start
/// at transport completion, so queued expiry cannot consume delivery credit.
struct VoiceRepairTracker {
    let started:TimeInterval,history:Int
    private let retryInterval:TimeInterval
    private var attempts:[UInt32:(count:Int,at:TimeInterval)]=[:]
    private var lastReservation:TimeInterval = -.infinity
    private var sequence:UInt32=0
    private(set) var pending:VoiceRepairRequest?
    private(set) var sent=0,rejected=0

    init(started:TimeInterval,history:Int,retryInterval:TimeInterval=VoicePolicy.repairRetry) {
        self.started=started;self.history=history;self.retryInterval=retryInterval
    }

    mutating func reserve(_ merge:VoiceMerge,now:TimeInterval)->VoiceRepairRequest? {
        guard pending==nil,VoiceControl.repairHistory.contains(history),now>=started,
              now-lastReservation>=VoicePolicy.repairInterval else {return nil}
        attempts=attempts.filter{$0.key>=merge.next && $0.key<merge.next+VoiceMerge.capacity}
        let frames=merge.repairCandidates(now:now,started:started,history:history).filter {
            guard let attempt=attempts[$0] else {return true}
            return attempt.count<VoicePolicy.repairAttempts && now-attempt.at>=retryInterval
        }
        guard !frames.isEmpty else {return nil}
        sequence &+= 1;if sequence==0 {sequence=1}
        let request=VoiceRepairRequest(id:sequence,frames:frames)
        pending=request;lastReservation=now
        return request
    }

    mutating func complete(_ id:UInt32,submission:VoiceRepairSubmission,now:TimeInterval) {
        guard let request=pending,request.id==id else {return}
        pending=nil
        guard submission != .notSubmitted else {rejected+=1;return}
        for frame in request.frames {attempts[frame]=((attempts[frame]?.count ?? 0)+1,now)}
        sent+=1
    }
}
