import Foundation

enum AppMetadata {
    static var version:String? { Bundle.main.object(forInfoDictionaryKey:"CFBundleShortVersionString") as? String }
    static var userAgent:String { "FoloToy-Vibe-Companion/\(version ?? "dev")" }
}
