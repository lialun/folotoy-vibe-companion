import Foundation

enum ShortcutSleepTests {
    static func run(_ check:(Bool,String)->Void) {
        var events:[(UInt16,Bool,UInt64)]=[]
        let keyboard=Shortcut(permissionCheck:{true},eventSink:{events.append(($0,$1,$2))})
        check(keyboard.inputReady && !Shortcut(permissionCheck:{false},eventSink:{_,_,_ in}).inputReady,"input readiness follows the injected keyboard port")
        check((try? keyboard.start("ctrl+option+v"))==true && events.count==3,"voice shortcut starts in an awake session")
        keyboard.suspendForSleep()
        check(events.count==6 && events.suffix(3).map{$0.0}==[9,58,59] && events.suffix(3).allSatisfy{!$0.1} && events.last?.2==0,"sleep releases held keys in reverse order and clears all modifier flags")
        let count=events.count
        check((try? keyboard.start("ctrl+option+v"))==false,"late voice start cannot press keys during sleep")
        keyboard.enter();keyboard.stop();keyboard.suspendForSleep()
        check(events.count==count,"sleep and repeated cleanup synthesize no new Enter or key events")
        keyboard.resume(if:{false})
        check((try? keyboard.start("ctrl+option+v"))==false,"a stale resume cannot re-enable shortcut delivery")
        keyboard.resume();keyboard.enter()
        check(events.count==count+2 && events.suffix(2).map{$0.1}==[true,false],"normal wake restores one Enter down/up pair")
        check((try? keyboard.start("ctrl+option+v"))==true,"voice shortcut works again after wake")
        keyboard.stop()
        check(events.last?.2==0,"post-wake cleanup leaves no held modifier")
    }
}
