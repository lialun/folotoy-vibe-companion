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
    static func hasElevatedConflictRisk(_ spec:String)->Bool {
        guard let keys=try? resolve(spec),let last=keys.last else { return true }
        // Advice only: applications do not expose a complete global shortcut registry.
        let modifierCount=keys.dropLast().filter { modifiers[$0] != nil }.count
        return modifiers[last] != nil || modifierCount<2
    }
    static func guidance(_ spec:String)->String {
        guard (try? resolve(spec)) != nil else { return "请使用有效组合，例如 ctrl+option+v。" }
        if hasElevatedConflictRisk(spec) { return "单键或单修饰键组合更易冲突；建议至少使用两个修饰键，避免单独 Shift 和常用编辑快捷键。" }
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
    func post(_ code:CGKeyCode,down:Bool) {
        if Self.modifiers[code] != nil { if down { flags.insert(code) } else { flags.remove(code) } }
        guard let e=CGEvent(keyboardEventSource:nil,virtualKey:code,keyDown:down) else { return }
        e.flags=CGEventFlags(rawValue:flags.reduce(UInt64(0)) { $0 | (Self.modifiers[$1] ?? 0) })
        if Self.modifiers[code] != nil { e.type = .flagsChanged }
        e.post(tap:.cghidEventTap)
    }
    func start(_ spec:String) throws {
        guard pressed.isEmpty else { return }
        guard Self.permitted else { throw ShortcutError.permission }
        for code in try Self.resolve(spec) { pressed.append(code); post(code,down:true) }
    }
    func stop() { for code in pressed.reversed() { post(code,down:false) }; pressed=[]; flags=[] }
    func enter() {
        guard Self.permitted, pressed.isEmpty else { return }
        post(36,down:true); Thread.sleep(forTimeInterval:0.04); post(36,down:false)
    }
    deinit { stop() }
}
