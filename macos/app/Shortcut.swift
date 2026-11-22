import AppKit
import CoreGraphics
import ApplicationServices

enum ShortcutError: Error, LocalizedError {
    case invalid, permission
    var errorDescription: String? { self == .invalid ? "快捷键格式不正确，例如 ctrl+option+v" : "请先为 Vibe 伴侣开启发送语音快捷键的权限" }
}
final class Shortcut {
    static let codes: [String:CGKeyCode] = ["a":0,"s":1,"d":2,"f":3,"h":4,"g":5,"z":6,"x":7,"c":8,"v":9,"b":11,"q":12,"w":13,"e":14,"r":15,"y":16,"t":17,"1":18,"2":19,"3":20,"4":21,"6":22,"5":23,"9":25,"7":26,"8":28,"0":29,"o":31,"u":32,"i":34,"p":35,"enter":36,"l":37,"j":38,"k":40,"n":45,"m":46,"tab":48,"space":49,"backspace":51,"escape":53,"command":55,"shift":56,"option":58,"ctrl":59,"right-shift":60,"right-option":61,"right-ctrl":62,"right-command":54,"fn":63]
    // Side-specific device masks matter: WeType distinguishes left and right modifiers.
    static let modifiers: [CGKeyCode:UInt64] = [55:(1<<20)|0x08,54:(1<<20)|0x10,56:(1<<17)|0x02,60:(1<<17)|0x04,58:(1<<19)|0x20,61:(1<<19)|0x40,59:(1<<18)|0x01,62:(1<<18)|0x2000,63:1<<23]
    static func resolve(_ spec:String) throws -> [CGKeyCode] {
        let aliases=["control":"ctrl","alt":"option","cmd":"command","left-ctrl":"ctrl","left-option":"option","left-shift":"shift","left-command":"command","return":"enter"]
        let words=spec.lowercased().split(separator:"+",omittingEmptySubsequences:false).map { String($0).trimmingCharacters(in:.whitespaces) }
        let keys=try words.map { word -> CGKeyCode in
            guard let code=codes[aliases[word] ?? word] else { throw ShortcutError.invalid }; return code
        }
        guard !keys.isEmpty, Set(keys).count==keys.count, keys.dropLast().allSatisfy({ modifiers[$0] != nil }) else { throw ShortcutError.invalid }
        return keys
    }
    static func recordedSpec(_ keys:[CGKeyCode])->String? {
        let names=keys.compactMap { code in codes.first(where:{$0.value==code})?.key }
        let value=names.joined(separator:"+")
        guard names.count==keys.count,(try? resolve(value)) != nil else { return nil }
        return value
    }
    static func recordingModifiers(_ raw:UInt64)->[CGKeyCode] {
        // Hardware events include device-side bits; some synthesized events
        // carry only the aggregate flag. Use the left key in that case.
        let groups:[(CGKeyCode,CGKeyCode,UInt64)]=[(59,62,1<<18),(58,61,1<<19),(56,60,1<<17),(55,54,1<<20)]
        var keys:[CGKeyCode]=[]
        for (left,right,aggregate) in groups where raw & aggregate != 0 {
            let leftDown=raw & (modifiers[left]! & 0xffff) != 0
            let rightDown=raw & (modifiers[right]! & 0xffff) != 0
            if leftDown || !rightDown { keys.append(left) }
            if rightDown { keys.append(right) }
        }
        if raw & (1<<23) != 0 { keys.append(63) }
        return keys
    }
    static func displayKeys(_ spec:String)->[String] {
        guard let keys=try? resolve(spec) else { return [spec] }
        let labels:[CGKeyCode:String]=[55:"左 ⌘",54:"右 ⌘",56:"左 ⇧",60:"右 ⇧",58:"左 ⌥",61:"右 ⌥",59:"左 ⌃",62:"右 ⌃",63:"Fn",36:"↩",48:"Tab",49:"空格",51:"⌫",53:"Esc"]
        return keys.map { code in labels[code] ?? codes.first(where:{$0.value==code})!.key.uppercased() }
    }
    static func hasElevatedConflictRisk(_ spec:String)->Bool {
        guard let keys=try? resolve(spec),let last=keys.last else { return true }
        // Advice only: applications do not expose a complete global shortcut registry.
        let modifierCount=keys.dropLast().filter { modifiers[$0] != nil }.count
        return modifiers[last] != nil || modifierCount<2
    }
    static func guidance(_ spec:String)->String {
        guard (try? resolve(spec)) != nil else { return "请使用有效组合，例如 ctrl+option+v。" }
        if hasElevatedConflictRisk(spec) { return "此组合容易冲突，建议使用两个修饰键。" }
        return "当前为多修饰键组合，仍需避开其他应用已使用的快捷键。"
    }
    // Sending CGEvents needs PostEvent access. Requiring unrelated AX UI access
    // as well can reject an otherwise valid event-posting grant.
    private static let permissionLock=NSLock()
    private static var permissionSnapshot=false
    // One main-thread preflight feeds UI, discovery and keyboard workers. Never
    // synchronously dispatch from a worker: shutdown can wait on that worker.
    static var permitted:Bool {
        if Thread.isMainThread {
            let value=CGPreflightPostEventAccess()
            permissionLock.lock();permissionSnapshot=value;permissionLock.unlock()
            return value
        }
        permissionLock.lock();defer { permissionLock.unlock() };return permissionSnapshot
    }
    static func requestPermission() {
        if CGRequestPostEventAccess() { return }
        // Register the app in the shared system list if an old ad-hoc identity
        // left it absent. AX is only a request route, never a readiness gate.
        if !AXIsProcessTrusted() {
            _=AXIsProcessTrustedWithOptions([kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String:true] as CFDictionary)
        }
    }
    private var pressed:[CGKeyCode]=[], flags:Set<CGKeyCode>=[]
    private let stateLock=NSLock()
    private var sleeping=false
    private let permissionCheck:()->Bool
    private let eventSink:((CGKeyCode,Bool,UInt64)->Void)?
    init(permissionCheck:@escaping ()->Bool={Shortcut.permitted},eventSink:((CGKeyCode,Bool,UInt64)->Void)?=nil) {
        self.permissionCheck=permissionCheck;self.eventSink=eventSink
    }
    private func post(_ code:CGKeyCode,down:Bool) {
        if Self.modifiers[code] != nil { if down { flags.insert(code) } else { flags.remove(code) } }
        let rawFlags=flags.reduce(UInt64(0)) { $0 | (Self.modifiers[$1] ?? 0) }
        if let eventSink=eventSink {eventSink(code,down,rawFlags);return}
        guard let e=CGEvent(keyboardEventSource:nil,virtualKey:code,keyDown:down) else { return }
        e.flags=CGEventFlags(rawValue:rawFlags)
        if Self.modifiers[code] != nil { e.type = .flagsChanged }
        e.post(tap:.cghidEventTap)
    }
    @discardableResult func start(_ spec:String) throws->Bool {
        stateLock.lock();defer{stateLock.unlock()}
        guard !sleeping else {return false}
        guard pressed.isEmpty else { return true }
        guard permissionCheck() else { throw ShortcutError.permission }
        for code in try Self.resolve(spec) { pressed.append(code); post(code,down:true) }
        return true
    }
    private func releaseKeys() { for code in pressed.reversed() { post(code,down:false) }; pressed=[]; flags=[] }
    func stop() {stateLock.lock();defer{stateLock.unlock()};releaseKeys()}
    func suspendForSleep() {
        stateLock.lock();defer{stateLock.unlock()}
        sleeping=true;releaseKeys()
    }
    func resume(if allowed:()->Bool={true}) {
        stateLock.lock();defer{stateLock.unlock()};if allowed() {sleeping=false}
    }
    func enter() {
        stateLock.lock();defer{stateLock.unlock()}
        guard !sleeping,permissionCheck(), pressed.isEmpty else { return }
        post(36,down:true); Thread.sleep(forTimeInterval:0.04); post(36,down:false)
    }
    deinit { stop() }
}
