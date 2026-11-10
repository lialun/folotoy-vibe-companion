import Foundation
import Darwin

// Account-local storage intentionally replaces Keychain: no keychain APIs or migration reads.
enum PairingStore {
    static var directory:URL {
        FileManager.default.urls(for:.applicationSupportDirectory,in:.userDomainMask)[0]
            .appendingPathComponent("io.github.vibecompanion.mac",isDirectory:true)
    }
    private static func validDevice(_ value:String)->Bool {
        value.count==12 && value.allSatisfy { "0123456789abcdef".contains($0) }
    }
    private static func validToken(_ value:String)->Bool {
        value.count==64 && value.allSatisfy { "0123456789abcdef".contains($0) }
    }
    static func load(_ device:String,directory:URL=directory)->String? {
        guard validDevice(device),let data=try? Data(contentsOf:directory.appendingPathComponent("pairing.json")),
              let pairs=try? JSONDecoder().decode([String:String].self,from:data),
              let token=pairs[device],validToken(token) else { return nil }
        return token
    }
    static func save(_ token:String,device:String,directory:URL=directory)->Bool {
        guard validDevice(device),validToken(token) else { return false }
        do {
            let files=FileManager.default
            try files.createDirectory(at:directory,withIntermediateDirectories:true,attributes:[.posixPermissions:0o700])
            try files.setAttributes([.posixPermissions:0o700],ofItemAtPath:directory.path)
            let file=directory.appendingPathComponent("pairing.json")
            var pairs:[String:String]=[:]
            if files.fileExists(atPath:file.path) {
                let data=try Data(contentsOf:file)
                pairs=(try? JSONDecoder().decode([String:String].self,from:data)) ?? [:]
            }
            pairs[device]=token
            try JSONEncoder().encode(pairs).write(to:file,options:.atomic)
            try files.setAttributes([.posixPermissions:0o600],ofItemAtPath:file.path)
            return true
        } catch { return false }
    }
    static func random()->String {
        var generator=SystemRandomNumberGenerator()
        return (0..<32).map { _ in String(format:"%02x",UInt8.random(in:0...255,using:&generator)) }.joined()
    }
}
