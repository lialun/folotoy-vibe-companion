import Foundation

enum BLEFrameTests {
    static func run(_ check:(Bool,String)->Void) {
        var audio=Data([6]);audio.append(Data(repeating:0x4a,count:816))
        var control=Data([2]);control.append(Data(repeating:0x62,count:100))
        let now=Date(timeIntervalSince1970:100)
        for mtu in [23,80,185,247] {
            let voice=BLEFrame.fragments(audio,identifier:1,limit:mtu)
            let messages=BLEFrame.fragments(control,identifier:2,limit:mtu)
            var receiver=BLEFrame(),received:[Data]=[]
            for fragment in voice.prefix(2) {if let packet=receiver.accept(fragment,now:now) {received.append(packet)}}
            for fragment in messages {if let packet=receiver.accept(fragment,now:now) {received.append(packet)}}
            for fragment in voice.dropFirst(2) {if let packet=receiver.accept(fragment,now:now.addingTimeInterval(0.04)) {received.append(packet)}}
            check(received==[control,audio],"BLE control can complete between audio fragments without discarding their prefix")
        }
        let voice=BLEFrame.fragments(audio,identifier:10,limit:80)
        let messages=BLEFrame.fragments(control,identifier:11,limit:23)
        var receiver=BLEFrame()
        _=receiver.accept(voice[0],now:now)
        _=receiver.accept(messages[0],now:now)
        var invalid=voice[1];invalid[5]&+=1
        _=receiver.accept(invalid,now:now)
        var completed:Data?
        for fragment in messages.dropFirst() {completed=receiver.accept(fragment,now:now) ?? completed}
        check(completed==control,"a malformed voice continuation cannot reset control assembly")
        receiver=BLEFrame()
        _=receiver.accept(voice[0],now:now)
        _=receiver.accept(messages[0],now:now.addingTimeInterval(1.5))
        check(receiver.accept(voice[1],now:now.addingTimeInterval(2.1))==nil,"expired voice prefix is rejected")
        completed=nil
        for fragment in messages.dropFirst() {completed=receiver.accept(fragment,now:now.addingTimeInterval(2.1)) ?? completed}
        check(completed==control,"voice expiry cannot reset a younger control packet")
        receiver=BLEFrame()
        _=receiver.accept(voice[0],now:now)
        var badControl=messages[0];badControl[2]=0xff
        _=receiver.accept(badControl,now:now)
        completed=nil
        for fragment in voice.dropFirst() {completed=receiver.accept(fragment,now:now) ?? completed}
        check(completed==audio,"malformed control cannot discard unrelated voice progress")
    }
}
