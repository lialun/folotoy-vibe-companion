import Foundation
import Combine
import AppKit
import ServiceManagement

final class AppModel: ObservableObject {
    let agents:AgentCenter
    let engine:Engine
    let preview:Bool
    @Published var state=ConnectionState()
    @Published var settings=DeviceSettings()
    @Published var code=""
    @Published var result=""
    @Published var saving=false
    @Published var loaded=false
    @Published var permissionChecking=false
    var onPermissionRequested:(()->Void)?
    private var confirmedSettings:DeviceSettings?
    @Published var shortcut=UserDefaults.standard.string(forKey:"shortcut") ?? "ctrl+option+v"
    @Published var cityQuery=""
    @Published var cities:[City]=[]
    @Published var searching=false
    @Published var radioStations:[RadioStation]=[]
    @Published var radioRevision=0
    @Published var radioLoaded=false
    @Published var radioLoading=false
    @Published var radioQuery=""
    @Published var radioChineseOnly=true
    @Published var radioCapacity=16
    @Published var customName=""
    @Published var customURL=""
    @Published var radioResults:[RadioStation]=[]
    @Published var radioSearching=false
    @Published var radioHasMore=false
    @Published var radioSource=0
    @Published var draggingStation:String?
    private var radioOffset=0,radioSearchGeneration=0
    private var radioSearchCriteria:(String,Bool)?
    @Published var radioChecking=false
    @Published var radioChecks:[String:String]=[:]
    private var radioBaseline:[RadioStation]=[]
    var filteredPresets:[RadioStation] { RadioStation.presets.filter { radioQuery.isEmpty || $0.name.localizedCaseInsensitiveContains(radioQuery) } }
    @Published var tab=0
    @Published var loginEnabled=SMAppService.mainApp.status == .enabled
    func requestKeyboardPermission() {
        onPermissionRequested?()
        Shortcut.requestPermission()
        NSWorkspace.shared.open(URL(string:"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!)
    }
    func permissionSession(visible:Bool)->PermissionSession {
        PermissionSession(settings:settings,confirmed:confirmedSettings,loaded:loaded,shortcut:shortcut,cityQuery:cityQuery,
            stations:radioStations,baseline:radioBaseline,revision:radioRevision,capacity:radioCapacity,radioLoaded:radioLoaded,
            query:radioQuery,chineseOnly:radioChineseOnly,source:radioSource,customName:customName,customURL:customURL,tab:tab,visible:visible)
    }
    func restore(_ session:PermissionSession) {
        settings=session.settings;confirmedSettings=session.confirmed;loaded=session.loaded
        shortcut=session.shortcut;cityQuery=session.cityQuery;radioStations=session.stations;radioBaseline=session.baseline
        radioRevision=session.revision;radioCapacity=session.capacity;radioLoaded=session.radioLoaded
        radioQuery=session.query;radioChineseOnly=session.chineseOnly;radioSource=session.source
        customName=session.customName;customURL=session.customURL;tab=session.tab
    }
    init(preview:Bool=false) {
        self.preview=preview
        if !preview { _=Shortcut.permitted }
        agents=AgentCenter(preview:preview)
        engine=Engine(preview:preview)
        agents.deliver={ [weak self] event in self?.engine.notifyAgent(event) }
        engine.onAgentResult={ [weak self] message in self?.agents.message=message }
        if !preview {
            do {
                if FileManager.default.fileExists(atPath:AgentInstaller.helper.path) { try AgentInstaller.prepareHelper() }
                try AgentInstaller.refreshPreparedPlugins()
            } catch { agents.message="通知组件更新失败："+error.localizedDescription }
        }
        if preview {
            state=ConnectionState(message:"界面预览",address:"不连接设备 · 示例数据",paired:true,connected:true,permission:true,audio:true)
            loaded=true;settings.weather_enabled=true;settings.city="北京";settings.screen_seconds_min=3;settings.appearance=AppearanceSettings()
            radioStations=Array(RadioStation.presets.prefix(4));radioRevision=1;radioLoaded=true;radioCapacity=RadioStation.maximumCount
            return
        }
        engine.onState = { [weak self] in self?.state=$0 }
        engine.onSettings = { [weak self] prefs in
            guard let self=self else { return }
            let previous=self.confirmedSettings
            self.settings=previous.map { self.settings.keepingEdits(over:prefs,basedOn:$0) } ?? prefs
            self.confirmedSettings=prefs;self.loaded=true
            if let previous=previous,previous != prefs,!self.saving {
                self.result=self.settings.changes(comparedTo:prefs).isEmpty ? "已同步设备设置" : "已同步设备设置，未保存的修改已保留"
            }
            if prefs.screen_seconds>60 {
                self.settings.screen_seconds=60
                self.result="设备原息屏时间为 \(prefs.screen_seconds) 秒，本版范围为 3–60 秒；保存时将改为 60 秒"
            }
        }
        engine.onResult = { [weak self] ok,message in
            guard let self=self else { return };self.saving=false
            let hasEdits=self.confirmedSettings.map { !self.settings.changes(comparedTo:$0).isEmpty } ?? false
            self.result=ok && hasEdits ? "已保存；还有新的修改尚未保存" : message
        }
        engine.onRadio = { [weak self] stations,revision,capacity in
            guard let self=self else { return }
            let dirty=self.radioLoaded && self.radioStations != self.radioBaseline
            if !dirty || self.saving { self.radioStations=stations } else { self.result="设备列表已更新，未保存的草稿已保留" }
            self.radioBaseline=stations;self.radioRevision=revision;self.radioCapacity=capacity;self.radioLoaded=true
        }
        engine.onRadioLoading = { [weak self] in self?.radioLoading=$0 }
        engine.onRadioVerified = { [weak self] in self?.radioChecks[$0]="设备已验证" }
        engine.start()
    }
    func save() {
        guard !preview else { result="这是界面预览，设置不会写入设备";return }
        if tab==4 {
            guard radioLoaded else { result="请先读取设备电台列表";return }
            saving=true;result="正在保存…";engine.saveRadio(radioStations,revision:radioRevision,capacity:radioCapacity);return
        }
        guard settings.screen_seconds >= (settings.screen_seconds_min ?? 10) else { result="当前设备固件的息屏下限为 10 秒，请先更新固件";return }
        guard settings.appearance?.valid != false else { result="亮色与暗色开始时间不能相同，请检查主题时间";return }
        guard settings.validForSave else { result="请检查数值范围、城市和电台地址；息屏时间应为 3–60 秒";return }
        guard let baseline=confirmedSettings else { return }
        let changes=settings.changes(comparedTo:baseline)
        guard !changes.isEmpty else { result="已与设备同步";return }
        saving=true;result="正在保存…";engine.save(settings,changes:changes)
    }
    func reloadCurrent() {
        if preview { result="预览不会读取设备";return }
        if let confirmed=confirmedSettings { settings=confirmed }
        result=""
        if tab==4 { radioStations=radioBaseline;engine.reloadRadio() } else { engine.reload() }
    }
    func addStation(_ station:RadioStation) {
        guard station.valid else { result="请输入有效的名称和 HTTP / HTTPS 音频流地址";return }
        guard !radioStations.contains(where:{$0.url==station.url}) else { result="这个电台已在列表中";return }
        guard radioStations.count<radioCapacity else { result="设备最多保存 \(radioCapacity) 个电台，请先移除一个";return }
        radioStations.append(station);result="已加入，保存后生效"
    }
    func moveStation(_ id:String,to target:String) {
        guard let from=radioStations.firstIndex(where:{$0.id==id}),let to=radioStations.firstIndex(where:{$0.id==target}),from != to else { return }
        let item=radioStations.remove(at:from);radioStations.insert(item,at:to);result="顺序已调整，保存后生效"
    }
    func moveStation(_ station:RadioStation,by delta:Int) {
        guard let index=radioStations.firstIndex(of:station),radioStations.indices.contains(index+delta) else { return }
        radioStations.swapAt(index,index+delta);result="顺序已调整，保存后生效"
    }
    func findStations(more:Bool=false) {
        guard !radioSearching else { return }
        let query=radioQuery.trimmingCharacters(in:.whitespacesAndNewlines),chinese=radioChineseOnly
        if !more || radioSearchCriteria?.0 != query || radioSearchCriteria?.1 != chinese { radioOffset=0;radioResults=[];radioHasMore=false }
        radioSearchCriteria=(query,chinese)
        radioSearching=true;radioSearchGeneration+=1;let generation=radioSearchGeneration
        RadioDirectory.search(name:query,chineseOnly:chinese,offset:radioOffset) { [weak self] response in
            guard let self=self,self.radioSearchGeneration==generation else { return }
            self.radioSearching=false
            guard self.radioQuery.trimmingCharacters(in:.whitespacesAndNewlines)==query,self.radioChineseOnly==chinese else { self.radioResults=[];self.radioHasMore=false;return }
            switch response {
            case .success(let (stations,more)):
                for station in stations where !self.radioResults.contains(where:{$0.url==station.url}) { self.radioResults.append(station) }
                self.radioHasMore=more;self.radioOffset+=RadioDirectory.pageSize
                if self.radioResults.isEmpty { self.result="没有找到兼容电台，可换个关键词或取消中文筛选" }
            case .failure(let error):self.result=error.localizedDescription
            }
        }
    }
    func checkCustomStation() {
        let station=RadioStation(name:customName.trimmingCharacters(in:.whitespacesAndNewlines),url:customURL.trimmingCharacters(in:.whitespacesAndNewlines))
        guard station.valid else { result="请填写电台名称和 HTTP／HTTPS 直播地址";return }
        guard !radioChecking else { return };radioChecking=true;result="正在检测…"
        RadioProbe.check(station) { [weak self] response in
            guard let self=self else { return };self.radioChecking=false
            switch response {
            case .success(let checked):self.radioChecks[checked.url]="已检测";self.addStation(checked)
            case .failure(let error):self.result=error.localizedDescription
            }
        }
    }
    func saveShortcut() {
        guard !preview else { result="这是界面预览，快捷键不会修改";return }
        do { try engine.setShortcut(shortcut);result="快捷键已保存，请在输入法中使用相同的「按住说话」快捷键" }
        catch { result=error.localizedDescription }
    }
    struct City: Identifiable,Decodable {
        let id:Int,name:String,latitude:Double,longitude:Double
        let admin1:String?,country:String?
        var description:String { [name,admin1,country].compactMap{$0}.joined(separator:" · ") }
    }
    func findCity() {
        guard !cityQuery.trimmingCharacters(in:.whitespaces).isEmpty else { return }
        searching=true;cities=[]
        var url=URLComponents(string:"https://geocoding-api.open-meteo.com/v1/search")!
        url.queryItems=[URLQueryItem(name:"name",value:cityQuery),URLQueryItem(name:"count",value:"8"),URLQueryItem(name:"language",value:"zh"),URLQueryItem(name:"format",value:"json")]
        URLSession.shared.dataTask(with:url.url!) { [weak self] data,response,error in
            struct Results:Decodable { var results:[City]? }
            let found=data.flatMap{try? JSONDecoder().decode(Results.self,from:$0).results} ?? []
            DispatchQueue.main.async { self?.searching=false;self?.cities=found;if found.isEmpty { self?.result="没有找到城市，可尝试拼音或英文名称" } }
        }.resume()
    }
}
