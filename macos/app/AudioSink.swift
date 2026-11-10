import Foundation
import AudioToolbox
import CoreAudio

enum AudioFailure: Error, LocalizedError {
    case unavailable, code(OSStatus)
    var errorDescription:String? { switch self { case .unavailable:return "未找到 BlackHole 2ch，请先安装音频驱动";case .code(let n):return "音频设备无法启动（\(n)），请检查 BlackHole" } }
}
final class AudioSink {
    private var queue:AudioQueueRef?
    private var buffers:[AudioQueueBufferRef]=[]
    private let lock=NSLock()
    private var samples:[Int16]=[], offset=0
    private(set) var running=false
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
        guard let uid=Self.deviceUID() else { throw AudioFailure.unavailable }
        var format=AudioStreamBasicDescription(mSampleRate:16000,mFormatID:kAudioFormatLinearPCM,mFormatFlags:kLinearPCMFormatFlagIsSignedInteger|kLinearPCMFormatFlagIsPacked,mBytesPerPacket:2,mFramesPerPacket:1,mBytesPerFrame:2,mChannelsPerFrame:1,mBitsPerChannel:16,mReserved:0)
        var q:AudioQueueRef?
        let e=AudioQueueNewOutput(&format,{ context,q,buffer in
            guard let context else { return }; Unmanaged<AudioSink>.fromOpaque(context).takeUnretainedValue().fill(q,buffer)
        },Unmanaged.passUnretained(self).toOpaque(),nil,nil,0,&q)
        guard e==noErr,let q else { throw AudioFailure.code(e) }; queue=q
        let device=uid as CFString
        var rawDevice=Unmanaged.passUnretained(device).toOpaque()
        var result=withExtendedLifetime(device) { AudioQueueSetProperty(q,kAudioQueueProperty_CurrentDevice,&rawDevice,UInt32(MemoryLayout<CFString>.size)) }
        if result != noErr { stop(); throw AudioFailure.code(result) }
        for _ in 0..<3 {
            var buffer:AudioQueueBufferRef?
            result=AudioQueueAllocateBuffer(q,640,&buffer)
            guard result==noErr,let buffer else { stop(); throw AudioFailure.code(result) }
            buffers.append(buffer); memset(buffer.pointee.mAudioData,0,640); buffer.pointee.mAudioDataByteSize=640
            AudioQueueEnqueueBuffer(q,buffer,0,nil)
        }
        result=AudioQueueStart(q,nil)
        guard result==noErr else { stop(); throw AudioFailure.code(result) }; running=true
    }
    func append(_ pcm:[Int16]) {
        lock.lock(); defer { lock.unlock() }
        if samples.count-offset>8000 { samples=[];offset=0 } // cap latency at 500ms under overload
        samples.append(contentsOf:pcm)
    }
    private func fill(_ q:AudioQueueRef,_ buffer:AudioQueueBufferRef) {
        let count=320; let output=buffer.pointee.mAudioData.assumingMemoryBound(to:Int16.self)
        lock.lock()
        let n=min(count,samples.count-offset)
        if n>0 { for i in 0..<n { output[i]=samples[offset+i] };offset+=n }
        if n<count { for i in n..<count { output[i]=0 } }
        if offset>=3200 { samples.removeFirst(offset);offset=0 }
        lock.unlock()
        buffer.pointee.mAudioDataByteSize=640; AudioQueueEnqueueBuffer(q,buffer,0,nil)
    }
    func stop() {
        if let q=queue { AudioQueueStop(q,true);AudioQueueDispose(q,true) };queue=nil;buffers=[];running=false
        lock.lock();samples=[];offset=0;lock.unlock()
    }
    deinit { stop() }
}
