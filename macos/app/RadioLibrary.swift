import Foundation

struct RadioStation:Codable,Equatable,Identifiable {
    static let maximumCount=20
    var name:String,url:String
    var id:String { url }
    var valid:Bool {
        guard !name.trimmingCharacters(in:.whitespacesAndNewlines).isEmpty,name.utf8.count<49,url.utf8.count<257,
              !name.unicodeScalars.contains(where:{$0.value<32 || $0.value==127}),
              !url.unicodeScalars.contains(where:{$0.value<33 || $0.value>126 || $0.value==92}),
              let address=URLComponents(string:url),["http","https"].contains(address.scheme ?? ""),
              address.user==nil,address.password==nil,let host=address.host,!host.isEmpty else { return false }
        return true
    }
    var object:[String:Any] { ["name":name,"url":url] }
    static let presets:[RadioStation]=[
        ("CNR-1 中国之声","https://lhttp.qtfm.cn/live/15318317/64k.mp3"),
        ("CNR-2 经济之声","http://ngcdn002.cnr.cn/live/jjzs/index.m3u8"),
        ("CNR-9 文艺之声","http://satellitepull.cnr.cn/live/wxwyzs/playlist.m3u8"),
        ("新闻联播","https://rfienchinois64k.ice.infomaniak.ch//rfienchinois-64.mp3"),
        ("CCTV-10科教伴音","https://piccpndali.v.myalicdn.com/audio/cctv10_2.m3u8"),
        ("第一财经","http://lhttp.qingting.fm/live/276/64k.mp3"),
        ("国际新闻","https://lhttp.qtfm.cn/live/20500172/64k.mp3"),
        ("CRI环球资迅广播","https://sk.cri.cn/905.m3u8"),
        ("500首华语经典","https://lhttp.qtfm.cn/live/5022308/64k.mp3"),
        ("SPH 963 Radio","http://playerservices.streamtheworld.com/api/livestream-redirect/HAO_963AAC.aac"),
        ("清晨音乐台","http://lhttp.qingting.fm/live/4915/64k.mp3"),
        ("90.7mixfm","https://lhttp.qingting.fm/live/15318146/64k.mp3"),
        ("德云社相声合集","https://stream.zeno.fm/yqawwmweq8mtv"),
        ("AsiaFM 亚洲粤语台","https://lhttp.qtfm.cn/live/15318569/64k.mp3"),
        ("香港普通话台","https://rthkradiopth-live.akamaized.net/hls/live/2040082/radiopth/master.m3u8"),
    ].map { RadioStation(name:$0.0,url:$0.1) }
}
enum RadioFailure:LocalizedError {
    case message(String)
    var errorDescription:String? { if case .message(let value)=self { return value };return nil }
}
struct RadioListTransfer {
    enum Stage { case read,begin,put,commit,done }
    private(set) var stage:Stage
    private(set) var stations:[RadioStation]
    private(set) var revision:Int
    private(set) var index=0
    private var count=0
    private(set) var capacity=RadioStation.maximumCount
    let transaction=UUID().uuidString
    let writing:Bool
    init() { stage = .read;stations=[];revision=0;writing=false }
    init(stations:[RadioStation],revision:Int,capacity:Int=RadioStation.maximumCount) throws {
        self.capacity=min(capacity,RadioStation.maximumCount)
        guard capacity>0 && stations.count<=self.capacity,stations.allSatisfy(\.valid),Set(stations.map(\.url)).count==stations.count,revision>0 else { throw RadioFailure.message("最多选择 \(self.capacity) 个不同电台，并先读取设备列表") }
        self.stations=stations;self.revision=revision;stage = .begin;writing=true
    }
    var command:(String,[String:Any]) {
        switch stage {
        case .read: return ("radio.get",index==0 ? ["index":0] : ["index":index,"revision":revision])
        case .begin:return ("radio.begin",["transaction":transaction,"revision":revision,"count":stations.count])
        case .put:return ("radio.put",["transaction":transaction,"index":index,"station":stations[index].object])
        case .commit:return ("radio.commit",["transaction":transaction])
        case .done:return ("",[:])
        }
    }
    mutating func accept(_ body:[String:Any]) throws -> Bool {
        guard body["ok"] as? Bool==true else { throw RadioFailure.message(body["error"] as? String ?? "电台同步失败") }
        switch stage {
        case .read:
            if index==0 { capacity=min(body["capacity"] as? Int ?? 16,RadioStation.maximumCount) }
            guard capacity>0 else { throw RadioFailure.message("设备电台容量无效") }
            if body["count"] as? Int==0,let rev=body["revision"] as? Int,rev>0,index==0 { revision=rev;stage = .done;return true }
            guard let rev=body["revision"] as? Int,rev>0,let size=body["count"] as? Int,(1...capacity).contains(size),body["index"] as? Int==index,
                  let entry=body["station"] as? [String:Any],let name=entry["name"] as? String,let url=entry["url"] as? String else { throw RadioFailure.message("设备电台列表格式无效") }
            if index==0 { revision=rev;count=size }
            guard revision==rev,count==size else { throw RadioFailure.message("列表已更改，请重新读取") }
            let station=RadioStation(name:name,url:url)
            guard station.valid,!stations.contains(where:{$0.url==url}) else { throw RadioFailure.message("设备电台名称或地址无效") }
            stations.append(station);index+=1;if index==count { stage = .done }
        case .begin:stage = stations.isEmpty ? .commit : .put
        case .put:index+=1;if index==stations.count { stage = .commit }
        case .commit:
            guard let rev=body["revision"] as? Int,rev>0,body["count"] as? Int==stations.count else { throw RadioFailure.message("保存结果不完整，请重新读取") }
            revision=rev;stage = .done
        case .done:throw RadioFailure.message("电台同步已结束")
        }
        return stage == .done
    }
}

enum RadioDirectory {
    static let pageSize=10
    struct Entry:Decodable {
        var name:String,url:String,url_resolved:String?,codec:String?,lastcheckok:Int?,hls:Int?
        var station:RadioStation? {
            guard ["MP3","AAC","AAC+"].contains(codec?.uppercased() ?? "") || (hls ?? 0)==1 else { return nil }
            let address=(url_resolved?.isEmpty==false ? url_resolved! : url)
            // Truncate directory names at a Character boundary; firmware stores 48 UTF-8 bytes.
            var title="";for ch in name.trimmingCharacters(in:.whitespacesAndNewlines) { if (title+String(ch)).utf8.count>=49 { break };title.append(ch) }
            let value=RadioStation(name:title,url:address);return value.valid ? value : nil
        }
    }
    static func request(name:String,chineseOnly:Bool,offset:Int=0,host:Int=0)->URLRequest {
        let hosts=["de1.api.radio-browser.info","fi1.api.radio-browser.info"]
        var url=URLComponents(string:"https://\(hosts[host])/json/stations/search")!
        url.queryItems=[.init(name:"limit",value:String(pageSize)),.init(name:"name",value:name)]
        if chineseOnly { url.queryItems!.append(.init(name:"language",value:"chinese")) }
        url.queryItems! += [.init(name:"hidebroken",value:"true"),.init(name:"order",value:"votes"),.init(name:"reverse",value:"true")]
        if offset>0 { url.queryItems!.append(.init(name:"offset",value:String(offset))) }
        var request=URLRequest(url:url.url!);request.timeoutInterval=8
        request.setValue(AppMetadata.userAgent,forHTTPHeaderField:"User-Agent")
        return request
    }
    static func search(name:String,chineseOnly:Bool,offset:Int,completion:@escaping(Result<([RadioStation],Bool),Error>)->Void) {
        query(name:name,chineseOnly:chineseOnly,offset:offset,host:0) { result in
            let mapped=result.map { entries -> ([RadioStation],Bool) in
                var stations:[RadioStation]=[]
                for station in entries.compactMap(\.station) where !stations.contains(where:{$0.url==station.url}) { stations.append(station) }
                return (stations,entries.count==pageSize)
            }
            DispatchQueue.main.async { completion(mapped) }
        }
    }
    private static func query(name:String,chineseOnly:Bool,offset:Int,host:Int,completion:@escaping(Result<[Entry],Error>)->Void) {
        URLSession.shared.dataTask(with:request(name:name,chineseOnly:chineseOnly,offset:offset,host:host)) { data,response,error in
            if error==nil,(response as? HTTPURLResponse)?.statusCode==200,let data=data,data.count<=1_048_576,let entries=try? JSONDecoder().decode([Entry].self,from:data) { completion(.success(entries)) }
            else if host<1 { query(name:name,chineseOnly:chineseOnly,offset:offset,host:host+1,completion:completion) }
            else { completion(.failure(error ?? RadioFailure.message("电台库暂时不可用，请稍后重试"))) }
        }.resume()
    }
}
