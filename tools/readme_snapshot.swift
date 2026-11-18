// Render the real SwiftUI view with preview-only sample data. No device,
// permissions, installed integrations or user preferences are modified.
import AppKit
import SwiftUI
import Darwin

@main
struct ReadmeSnapshot {
    @MainActor static func main() throws {
        guard CommandLine.arguments.count == 2 else {
            throw NSError(domain: "ReadmeSnapshot", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "Expected an output directory"])
        }
        let app = NSApplication.shared
        app.setActivationPolicy(.prohibited)
        let model = AppModel(preview: true)
        model.state=ConnectionState(message:"已连接",device:"artwork-6bac",paired:true,connected:true,
            devices:[DiscoveredDevice(id:"artwork-6bac",endpoint:sockaddr_in(),paired:true,busy:false,selection:false,seen:Date(),bleID:UUID(),bleSeen:Date(),wifiSeen:Date())],
            wifiConnected:true,bleConnected:true,permission:true,audio:true)
        model.shortcut="ctrl+shift+v"
        model.settings.radio_list_version = 1
        model.radioStations = [0, 1, 2, 8, 10, 13].map { RadioStation.presets[$0] }
        model.agents.enabled = true
        model.agents.integrations = AgentID.allCases.map {
            AgentIntegration(agent: $0,
                             executable: URL(fileURLWithPath: "/usr/local/bin/" + $0.rawValue),
                             application: nil,
                             installed: [.codex, .claude, .opencode].contains($0))
        }
        let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        model.tab = 0
        try capture(model, size:NSSize(width:900,height:780),
                    to:output.appendingPathComponent("desktop-ui.png"))
        model.tab = 4
        try capture(model, size: NSSize(width: 980, height: 690),
                    to: output.appendingPathComponent("vibe-companion-desktop-radio.png"))
        model.tab = 5
        try capture(model, size: NSSize(width: 980, height: 1100),
                    to: output.appendingPathComponent("vibe-companion-desktop-agents.png"))
    }

    @MainActor private static func capture(_ model: AppModel, size: NSSize,
                                          to output: URL) throws {
        let view = NSHostingView(rootView: CompanionView(model: model)
            .frame(width:size.width,height:size.height))
        let bounds = NSRect(origin: .zero, size: size)
        let window = NSWindow(contentRect: bounds, styleMask: .borderless,
                              backing: .buffered, defer: false)
        window.contentView = view
        window.appearance=NSAppearance(named:.darkAqua)
        view.frame = bounds
        view.layoutSubtreeIfNeeded()
        RunLoop.current.run(until: Date().addingTimeInterval(0.2))
        guard let bitmap = view.bitmapImageRepForCachingDisplay(in: bounds) else {
            throw NSError(domain: "ReadmeSnapshot", code: 2)
        }
        view.cacheDisplay(in: bounds, to: bitmap)
        guard let png = bitmap.representation(using: .png, properties: [:]) else {
            throw NSError(domain: "ReadmeSnapshot", code: 3)
        }
        try png.write(to: output)
    }
}
