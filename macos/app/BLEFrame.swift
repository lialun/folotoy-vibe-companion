import Foundation

struct BLEFrame {
    private struct Assembly {
        let identifier:UInt8,total:Int,started:Date
        var data=Data()
    }
    private var voice:Assembly?,control:Assembly?

    mutating func accept(_ fragment:Data,now:Date=Date())->Data? {
        expire(now:now)
        let bytes=Array(fragment)
        guard bytes.count>7,bytes[0]==0x56,bytes[2]&0xfc==0 else {
            if bytes.count>1 {discard(identifier:bytes[1])}
            return nil
        }
        let identifier=bytes[1],total=Int(bytes[3])|Int(bytes[4])<<8
        let offset=Int(bytes[5])|Int(bytes[6])<<8
        guard total>0,total<=1537,offset+bytes.count-7<=total else {
            discard(identifier:identifier);return nil
        }
        let audio:Bool
        if bytes[2]&1 != 0 {
            audio=bytes[7]==1 || bytes[7]==6
            var state=Assembly(identifier:identifier,total:total,started:now)
            state.data.reserveCapacity(total)
            if audio {
                if control?.identifier==identifier {control=nil}
                voice=state
            } else {
                if voice?.identifier==identifier {voice=nil}
                control=state
            }
        } else if voice?.identifier==identifier {audio=true}
        else if control?.identifier==identifier {audio=false}
        else {return nil}
        return audio ? Self.feed(bytes,total:total,offset:offset,state:&voice) :
            Self.feed(bytes,total:total,offset:offset,state:&control)
    }

    private static func feed(_ bytes:[UInt8],total:Int,offset:Int,state:inout Assembly?)->Data? {
        guard state?.total==total,state?.data.count==offset else {state=nil;return nil}
        state!.data.append(contentsOf:bytes.dropFirst(7))
        guard bytes[2]&2 != 0 else {return nil}
        let result=state!.data.count==total ? state!.data:nil
        state=nil
        return result
    }

    private mutating func discard(identifier:UInt8) {
        if voice?.identifier==identifier {voice=nil}
        if control?.identifier==identifier {control=nil}
    }

    private mutating func expire(now:Date) {
        if let value=voice,now<value.started || now.timeIntervalSince(value.started)>2 {voice=nil}
        if let value=control,now<value.started || now.timeIntervalSince(value.started)>2 {control=nil}
    }

    static func fragments(_ packet:Data,identifier:UInt8,limit:Int,media:Bool=false)->[Data] {
        guard !packet.isEmpty,packet.count<=1537,limit>7 else { return [] }
        let bytes=Array(packet),step=limit-7
        return stride(from:0,to:bytes.count,by:step).map { offset in
            let end=min(offset+step,bytes.count)
            var frame=Data([0x56,identifier,(offset==0 ? 1:0)|(end==bytes.count ? 2:0)|(media ? 4:0),UInt8(bytes.count&255),UInt8(bytes.count>>8),UInt8(offset&255),UInt8(offset>>8)])
            frame.append(contentsOf:bytes[offset..<end]);return frame
        }
    }
}
