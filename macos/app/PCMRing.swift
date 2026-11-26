import Foundation

/// Fixed storage: appending never reallocates and overload is an explicit
/// failure instead of silently deleting the preceding half-second of speech.
struct PCMRing {
    private var samples:[Int16]
    private var readIndex=0,writeIndex=0
    private(set) var count=0
    var capacity:Int {samples.count}
    init(capacity:Int=VoicePolicy.pcmCapacity) {samples=[Int16](repeating:0,count:capacity)}

    mutating func append(_ pcm:[Int16])->Bool {
        guard pcm.count<=capacity-count else {return false}
        for value in pcm {
            samples[writeIndex]=value
            writeIndex=(writeIndex+1)%capacity
        }
        count+=pcm.count
        return true
    }

    mutating func read(into output:UnsafeMutablePointer<Int16>,maximum:Int)->Int {
        let n=min(maximum,count)
        for i in 0..<n {
            output[i]=samples[readIndex]
            readIndex=(readIndex+1)%capacity
        }
        count-=n
        return n
    }

    mutating func clear() {readIndex=0;writeIndex=0;count=0}
}
