import Foundation
import AudioToolbox
import CoreAudio

enum AudioFailure: Error, LocalizedError {
    case unavailable, code(OSStatus)
    var errorDescription:String? { switch self { case .unavailable:return "未找到 BlackHole 2ch，请先安装音频驱动";case .code(let n):return "音频设备无法启动（\(n)），请检查 BlackHole" } }
}
final class AudioSink:VoicePCMOutput {
    private var metrics=VoiceOutputMetrics()
    private var continuity=PCMContinuity()
    private var queue:AudioQueueRef?
    private var buffers:[AudioQueueBufferRef]=[]
    private let lock=NSLock()
    private var samples=PCMRing()
    private var inFlight:[UInt:Int]=[:]
    private var healthy=false
    var running:Bool {lock.lock();defer{lock.unlock()};return healthy}
    static func deviceUID() -> String? {
        var address=AudioObjectPropertyAddress(mSelector:kAudioHardwarePropertyDevices,mScope:kAudioObjectPropertyScopeGlobal,mElement:kAudioObjectPropertyElementMain)
        var size:UInt32=0
        guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject),&address,0,nil,&size)==noErr else { return nil }
        var devices=[AudioDeviceID](repeating:0,count:Int(size)/MemoryLayout<AudioDeviceID>.size)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject),&address,0,nil,&size,&devices)==noErr else { return nil }
        for device in devices {
            var prop=AudioObjectPropertyAddress(mSelector:kAudioObjectPropertyName,mScope:kAudioObjectPropertyScopeGlobal,mElement:kAudioObjectPropertyElementMain)
            var name:Unmanaged<CFString>?; var n=UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
            guard AudioObjectGetPropertyData(device,&prop,0,nil,&n,&name)==noErr,let name else { continue }
            guard (name.takeRetainedValue() as String).localizedCaseInsensitiveContains("BlackHole 2ch") else { continue }
            prop.mSelector=kAudioDevicePropertyDeviceUID; var uid:Unmanaged<CFString>?; n=UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
            if AudioObjectGetPropertyData(device,&prop,0,nil,&n,&uid)==noErr,let uid { return uid.takeRetainedValue() as String }
        }
        return nil
    }
    func start() throws {
        stop()
        lock.lock();metrics=VoiceOutputMetrics();continuity=PCMContinuity();lock.unlock()
        guard let uid=Self.deviceUID() else { throw AudioFailure.unavailable }
        var format=AudioStreamBasicDescription(mSampleRate:VoiceCodec.sampleRate,mFormatID:kAudioFormatLinearPCM,mFormatFlags:kLinearPCMFormatFlagIsSignedInteger|kLinearPCMFormatFlagIsPacked,mBytesPerPacket:2,mFramesPerPacket:1,mBytesPerFrame:2,mChannelsPerFrame:1,mBitsPerChannel:16,mReserved:0)
        var q:AudioQueueRef?
        let e=AudioQueueNewOutput(&format,{ context,q,buffer in
            guard let context else { return }; Unmanaged<AudioSink>.fromOpaque(context).takeUnretainedValue().fill(q,buffer)
        },Unmanaged.passUnretained(self).toOpaque(),nil,nil,0,&q)
        guard e==noErr,let q else { throw AudioFailure.code(e) }; queue=q
        let device=uid as CFString
        var rawDevice=Unmanaged.passUnretained(device).toOpaque()
        var result=withExtendedLifetime(device) { AudioQueueSetProperty(q,kAudioQueueProperty_CurrentDevice,&rawDevice,UInt32(MemoryLayout<CFString>.size)) }
        if result != noErr { stop(); throw AudioFailure.code(result) }
        for _ in 0..<VoicePolicy.outputBufferCount {
            var buffer:AudioQueueBufferRef?
            result=AudioQueueAllocateBuffer(q,UInt32(VoicePolicy.outputBufferSamples*2),&buffer)
            guard result==noErr,let buffer else { stop(); throw AudioFailure.code(result) }
            buffers.append(buffer); memset(buffer.pointee.mAudioData,0,VoicePolicy.outputBufferSamples*2); buffer.pointee.mAudioDataByteSize=UInt32(VoicePolicy.outputBufferSamples*2)
            result=AudioQueueEnqueueBuffer(q,buffer,0,nil)
            guard result==noErr else {stop();throw AudioFailure.code(result)}
        }
        lock.lock();healthy=true;lock.unlock()
        result=AudioQueueStart(q,nil)
        guard result==noErr else { stop(); throw AudioFailure.code(result) }
    }
    @discardableResult func append(_ pcm:[Int16])->Bool {
        lock.lock(); defer { lock.unlock() }
        guard samples.append(pcm) else {metrics.dropped+=pcm.count;return false}
#if VIBE_VOICE_BENCH
        for value in pcm {
            let bits=UInt16(bitPattern:value)
            metrics.fixtureAppendHash=(metrics.fixtureAppendHash ^ UInt32(bits&255)) &* 16777619
            metrics.fixtureAppendHash=(metrics.fixtureAppendHash ^ UInt32(bits>>8)) &* 16777619
        }
#endif
        metrics.appended+=pcm.count;metrics.highWater=max(metrics.highWater,samples.count)
        return true
    }
    func snapshot()->VoiceOutputMetrics {
        lock.lock();defer{lock.unlock()}
        var value=metrics
        value.queued=samples.count
        value.inFlight=inFlight.values.reduce(0,+)
        value.interruptions=continuity.interruptions
        value.interruptedSamples=continuity.samples
        value.longestInterruption=continuity.longest
        return value
    }
    private func fill(_ q:AudioQueueRef,_ buffer:AudioQueueBufferRef) {
        let count=VoicePolicy.outputBufferSamples; let output=buffer.pointee.mAudioData.assumingMemoryBound(to:Int16.self)
        lock.lock()
        let key=UInt(bitPattern:buffer)
        metrics.rendered+=inFlight[key] ?? 0
        inFlight[key]=nil
        guard healthy else {lock.unlock();return}
        let n=samples.read(into:output,maximum:count)
        continuity.consume(n,requested:count)
#if VIBE_VOICE_BENCH
        for i in 0..<n {
            let bits=UInt16(bitPattern:output[i])
            metrics.fixtureReadHash=(metrics.fixtureReadHash ^ UInt32(bits&255)) &* 16777619
            metrics.fixtureReadHash=(metrics.fixtureReadHash ^ UInt32(bits>>8)) &* 16777619
        }
#endif
        inFlight[key]=n
        metrics.underrun+=count-n
        if n<count { for i in n..<count { output[i]=0 } }
        lock.unlock()
        buffer.pointee.mAudioDataByteSize=UInt32(count*2)
        if AudioQueueEnqueueBuffer(q,buffer,0,nil) != noErr {
            lock.lock()
            metrics.dropped+=inFlight.removeValue(forKey:key) ?? 0
            healthy=false
            lock.unlock()
        }
    }
    func stop() {
        lock.lock();healthy=false;lock.unlock()
        if let q=queue { AudioQueueStop(q,true);AudioQueueDispose(q,true) };queue=nil;buffers=[]
        lock.lock()
        metrics.dropped+=samples.count+inFlight.values.reduce(0,+)
        samples.clear();inFlight.removeAll(keepingCapacity:true)
        lock.unlock()
    }
    deinit { stop() }
}
