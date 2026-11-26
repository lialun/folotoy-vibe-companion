import Foundation

/// Own complete packets so expiry/cancellation and completion have one logical
/// identity. Only the adapter knows whether the head fragment is in flight.
struct BLEWriteBuffer {
    struct Repair:Equatable {let id:UInt64,deadline:TimeInterval}
    struct RepairResult:Equatable {let id:UInt64,submission:VoiceRepairSubmission}
    struct Removal {let headRemoved:Bool,repairs:[RepairResult]}
    private struct Packet {
        let fragments:[Data],radio:Bool,key:Data?,repair:Repair?
        var voice=false
        var feedback=false
        var offset=0
        var cancelled=false
        var remaining:Int {fragments.count-offset}
        var first:Data {fragments[offset]}
        var kind:BLEWritePacing.Kind {radio ? .radio:voice ? .voice:.control}
    }
    private var packets=[Packet]()
    var firstIsRadio:Bool {packets.first?.radio ?? false}
    var firstKind:BLEWritePacing.Kind? {packets.first?.kind}
    var firstCancelled:Bool {packets.first?.cancelled ?? false}
    var first:Data? {packets.first?.first}
    var isEmpty:Bool {packets.isEmpty}
    var count:Int {packets.reduce(0){$0+$1.remaining}}

    mutating func append(_ frames:[Data],radio:Bool,key:Data?=nil,repair:Repair?=nil,voice:Bool=false,feedback:Bool=false,writing:Bool=false)->Bool {
        guard !frames.isEmpty,!(radio && (repair != nil || voice)) else {return false}
        if radio,let key,packets.contains(where:{$0.key==key}) {return true}
        if repair != nil,packets.contains(where:{$0.repair != nil}) {return false}
        if feedback {
            packets=packets.enumerated().filter {index,packet in
                !packet.feedback || (index==0 && (writing || packet.offset>0))
            }.map{$0.element}
        }
        guard count+frames.count <= (radio ? 128:256) else {return false}
        let packet=Packet(fragments:frames,radio:radio,key:key,repair:repair,voice:voice || repair != nil,feedback:feedback)
        // Preserve an actual assembly, including its first ATT write awaiting
        // acknowledgement. A pacing delay has no assembly to protect.
        if packet.voice,!packets.isEmpty {
            let first=writing || packets[0].offset>0 ? 1:0
            let index=packets[first...].firstIndex(where:{!$0.voice}) ?? packets.endIndex
            packets.insert(packet,at:index)
        }
        else {packets.append(packet)}
        return true
    }

    /// A paced, unstarted packet owns no peer assembly. Another eligible
    /// packet can use that idle interval; an actual prefix remains indivisible.
    mutating func selectReady(_ ready:(BLEWritePacing.Kind)->Bool) {
        guard let first=packets.first,first.offset==0,!first.cancelled,!ready(first.kind),
              let index=packets.indices.dropFirst().first(where:{ready(packets[$0].kind)}) else {return}
        let packet=packets.remove(at:index);packets.insert(packet,at:0)
    }

    @discardableResult mutating func acknowledge()->RepairResult? {
        guard !packets.isEmpty else {return nil}
        if packets[0].remaining>1 {packets[0].offset+=1;return nil}
        let packet=packets.removeFirst()
        return packet.repair.map{RepairResult(id:$0.id,submission:.submitted)}
    }

    @discardableResult mutating func reset(writing:Bool=false)->[RepairResult] {
        let results=packets.enumerated().compactMap{index,packet in
            packet.repair.map{RepairResult(id:$0.id,submission:writing && index==0 && packet.remaining==1 ? .uncertain:.notSubmitted)}
        }
        packets=[]
        return results
    }

    @discardableResult mutating func discardVoiceRepair(writing:Bool)->Removal {
        remove(writing:writing){$0.repair != nil}
    }

    mutating func discardExpiredRepairs(now:TimeInterval,writing:Bool=false)->Removal {
        remove(writing:writing){$0.repair.map{now >= $0.deadline} ?? false}
    }

    @discardableResult mutating func acknowledgeRadio(session:UInt64,next:UInt32,mask:UInt16,writing:Bool)->Removal {
        remove(writing:writing){packet in
            guard packet.radio,let key=packet.key,key.count==13 else {return false}
            let bytes=Array(key)
            let nonce=(0..<8).reduce(UInt64(0)){$0|UInt64(bytes[1+$1])<<($1*8)}
            let sequence=(0..<4).reduce(UInt32(0)){$0|UInt32(bytes[9+$1])<<($1*8)}
            let offset=sequence &- next
            return nonce != session || offset>=0x80000000 || (offset<16 && mask&(1<<offset) != 0)
        }
    }

    @discardableResult mutating func discardRadio(writing:Bool)->Removal {remove(writing:writing){$0.radio}}

    private mutating func remove(writing:Bool,where matches:(Packet)->Bool)->Removal {
        var kept=[Packet](),results=[RepairResult](),headRemoved=false
        for (index,packet) in packets.enumerated() {
            guard matches(packet) else {kept.append(packet);continue}
            let active=writing && index==0
            if let repair=packet.repair {
                results.append(RepairResult(id:repair.id,submission:active && packet.remaining==1 ? .uncertain:.notSubmitted))
            }
            if active {kept.append(Packet(fragments:[packet.first],radio:packet.radio,key:packet.key,repair:nil,cancelled:true))}
            else if index==0 {headRemoved=true}
        }
        packets=kept
        return Removal(headRemoved:headRemoved,repairs:results)
    }
}
