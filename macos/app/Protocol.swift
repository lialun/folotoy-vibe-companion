import Foundation

enum VoiceCodec {
    // Wire-compatible with the community firmware's independent 100ms IMA blocks.
    static let steps = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
    static let changes = [-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8]
    static func decode(_ data: Data) -> [Int16]? {
        guard data.count == 804 else { return nil }
        let bytes = Array(data)
        guard bytes[2] <= 88, bytes[3] == 0 else { return nil }
        var predictor = Int(Int16(bitPattern: UInt16(bytes[0]) | UInt16(bytes[1]) << 8))
        var index = Int(bytes[2]); var samples = [Int16(predictor)]
        samples.reserveCapacity(1600)
        for n in 0..<1599 {
            let code = Int((bytes[4 + n / 2] >> (n % 2 * 4)) & 15)
            let step = steps[index]
            var delta = step >> 3
            if code & 4 != 0 { delta += step }
            if code & 2 != 0 { delta += step >> 1 }
            if code & 1 != 0 { delta += step >> 2 }
            predictor = max(-32768,min(32767,predictor + (code & 8 != 0 ? -delta : delta)))
            index = max(0,min(88,index + changes[code])); samples.append(Int16(predictor))
        }
        return samples
    }
}

struct AppearanceSettings:Codable,Equatable {
    // Matches the device protocol: 0 dark, 1 light, 2 local-time schedule.
    var mode=2,light_start=480,dark_start=1260
    var valid:Bool { (0...2).contains(mode) && (0..<1440).contains(light_start) && (0..<1440).contains(dark_start) && light_start != dark_start }
}
struct DeviceSettings: Codable, Equatable {
    var brightness = 50, volume = 80, screen_seconds = 15, focus_minutes = 25, break_minutes = 5, mic_gain = 30
    var latitude = 39.9, longitude = 116.4, weather_enabled = true
    var city = "北京", radio_name = "经济之声", radio_url = "http://ngcdn002.cnr.cn/live/jjzs/index.m3u8"
    var radio_list_version:Int?,screen_seconds_min:Int?
    var appearance:AppearanceSettings?
    var valid: Bool {
        (10...100).contains(brightness) && (0...100).contains(volume) && (3...300).contains(screen_seconds) &&
        (1...180).contains(focus_minutes) && (1...60).contains(break_minutes) && (0...42).contains(mic_gain) &&
        latitude.isFinite && longitude.isFinite && abs(latitude)<=90 && abs(longitude)<=180 &&
        (appearance?.valid ?? true) && city.utf8.count<49 && radio_name.utf8.count<49 && radio_url.utf8.count<257 &&
        (radio_url.hasPrefix("http://") || radio_url.hasPrefix("https://")) && !radio_url.contains("\n") && !radio_url.contains("\r")
    }
    // Continue reading legacy devices' 61–300s settings without losing the reply.
    var validForSave:Bool { valid && (3...60).contains(screen_seconds) }
    mutating func requireWeather() {
        weather_enabled=true
        if city.isEmpty { city="北京";latitude=39.9;longitude=116.4 }
    }
    var object: [String: Any] {
        var value=self;value.requireWeather()
        return (try? JSONSerialization.jsonObject(with: JSONEncoder().encode(value))) as? [String:Any] ?? [:]
    }
    // Only locally edited fields are written; unrelated device edits survive.
    func changes(comparedTo baseline:Self)->[String:Any] {
        let old=baseline.object
        return object.filter { key,value in key != "screen_seconds_min" && !(value as! NSObject).isEqual(old[key]) }
    }
    func keepingEdits(over remote:Self,basedOn baseline:Self)->Self {
        let merged=remote.object.merging(changes(comparedTo:baseline)) { _,local in local }
        guard let data=try? JSONSerialization.data(withJSONObject:merged),let value=try? JSONDecoder().decode(Self.self,from:data) else { return self }
        return value
    }
}

enum Packet {
    case audio(sequence: UInt8, samples: [Int16]), event([String:Any]), ping
    static func parse(_ data: Data) -> Packet? {
        guard let type=data.first else { return nil }
        if type==4 && data.count==1 { return .ping }
        if type==1 {
            guard data.count==807, data[2]==0x80, let pcm=VoiceCodec.decode(data.subdata(in: 3..<807)) else { return nil }
            return .audio(sequence:data[1],samples:pcm)
        }
        if type==2, data.count<=1537, let o=(try? JSONSerialization.jsonObject(with:data.dropFirst())) as? [String:Any] { return .event(o) }
        return nil
    }
}
