import SwiftUI
import AppKit
import ServiceManagement
import UniformTypeIdentifiers

enum Palette {
    static let background=Color(red:0.065,green:0.10,blue:0.11)
    static let sidebar=Color(red:0.055,green:0.085,blue:0.09)
    static let surface=Color(red:0.10,green:0.15,blue:0.16)
    static let line=Color.white.opacity(0.08)
    static let accent=Color(red:0.66,green:0.85,blue:0.69)
    static let text=Color(red:0.90,green:0.92,blue:0.88)
    static let muted=Color(red:0.59,green:0.67,blue:0.65)
}
struct CompanionView:View {
    @ObservedObject var model:AppModel
    private let sections=[("连接与语音","point.3.connected.trianglepath.dotted"),("设备设置","slider.horizontal.3"),("番茄钟","timer"),("天气","cloud.sun"),("收音机","radio"),("Agent 提醒","bell.badge")]
    var body:some View {
        HStack(spacing:0) {
            sidebar.frame(width:178)
            Rectangle().fill(Palette.line).frame(width:1)
            VStack(alignment:.leading,spacing:0) {
                HStack(alignment:.top) {
                    VStack(alignment:.leading,spacing:7) {
                        Text(sections[model.tab].0).font(.system(size:25,weight:.semibold))
                    }
                    Spacer()
                    HStack(spacing:6) { Circle().fill(model.state.connected ? Palette.accent : .orange).frame(width:6,height:6);Text(model.state.recording ? "正在输入" : model.state.connected ? "设备在线" : "等待设备").font(.system(size:11,weight:.medium)) }
                        .padding(.horizontal,11).padding(.vertical,7).background(Palette.surface,in:Capsule())
                }.padding(.horizontal,28).padding(.top,28).padding(.bottom,22)
                ScrollView {
                    VStack(alignment:.leading,spacing:16) {
                        if !model.state.error.isEmpty { Label(model.state.error,systemImage:"exclamationmark.circle").font(.callout).foregroundStyle(.orange).textSelection(.enabled) }
                        switch model.tab {
                        case 0: connection
                        case 1: device
                        case 2: pomodoro
                        case 3: weather
                        case 4: radio
                        default: AgentSettingsView(center:model.agents,preview:model.preview)
                        }
                    }.padding(.horizontal,28).padding(.bottom,24).frame(maxWidth:.infinity,alignment:.leading)
                }
                footer
            }
        }.background(Palette.background).foregroundStyle(Palette.text).tint(Palette.accent).preferredColorScheme(.dark)
        .frame(minWidth:820,minHeight:610)
    }
    private var sidebar:some View {
        VStack(alignment:.leading,spacing:0) {
            HStack(spacing:10) { BrandMark(size:34);VStack(alignment:.leading,spacing:2) { Text("Vibe 伴侣").font(.system(size:14,weight:.semibold));Text("FOLOTOY").font(.system(size:8,weight:.medium,design:.monospaced)).tracking(2).foregroundStyle(Palette.muted) } }.padding(.top,29).padding(.horizontal,18).padding(.bottom,32)
            ForEach(sections.indices,id:\.self) { index in
                Button { model.tab=index } label: {
                    HStack(spacing:10) { Image(systemName:sections[index].1).font(.system(size:14)).frame(width:18);Text(sections[index].0).font(.system(size:12,weight:model.tab==index ? .semibold : .regular));Spacer() }
                        .foregroundStyle(model.tab==index ? Palette.accent : Palette.muted).padding(.horizontal,13).padding(.vertical,12)
                        .frame(maxWidth:.infinity,alignment:.leading).contentShape(Rectangle())
                        .background(model.tab==index ? Palette.surface : .clear,in:RoundedRectangle(cornerRadius:9))
                }.buttonStyle(.plain).padding(.horizontal,10).padding(.bottom,4).accessibilityLabel(sections[index].0)
            }
            Spacer()
            VStack(alignment:.leading,spacing:12) {
                Toggle("登录时启动",isOn:$model.loginEnabled).toggleStyle(.switch).controlSize(.mini).font(.system(size:11)).onChange(of:model.loginEnabled) { enabled in
                    do { if model.preview { model.result="界面预览不会更改登录项";return }; if enabled { try SMAppService.mainApp.register() } else { try SMAppService.mainApp.unregister() } }
                    catch { model.result=error.localizedDescription;model.loginEnabled=SMAppService.mainApp.status == .enabled }
                }
                if let version=AppMetadata.version {
                    Text("v"+version).font(.system(size:9,design:.monospaced)).foregroundStyle(Palette.muted.opacity(0.6))
                }
            }.padding(20)
        }.frame(maxHeight:.infinity).background(Palette.sidebar)
    }
    private func card<Content:View>(_ title:String,_ symbol:String,@ViewBuilder content:()->Content)->some View {
        VStack(alignment:.leading,spacing:17) {
            Label(title,systemImage:symbol).font(.system(size:12,weight:.semibold)).foregroundStyle(Palette.accent)
            content()
        }.padding(20).frame(maxWidth:.infinity,alignment:.leading).background(Palette.surface,in:RoundedRectangle(cornerRadius:14))
            .overlay(RoundedRectangle(cornerRadius:14).stroke(Palette.line,lineWidth:1))
    }
    private var connection:some View {
        VStack(spacing:16) {
            card("你的 FoloToy","link") {
                HStack(spacing:16) {
                    BrandMark(size:48)
                    VStack(alignment:.leading,spacing:6) { Text(model.state.message).font(.system(size:16,weight:.medium));Text(model.state.connected ? model.state.address : "电脑与设备需连接同一局域网").font(.system(size:11,design:.monospaced)).foregroundStyle(Palette.muted) }
                    Spacer();Button { model.engine.retry() } label: { Image(systemName:"arrow.clockwise") }.help("重新连接").buttonStyle(.borderless)
                }
                if !model.state.paired {
                    Divider().overlay(Palette.line)
                    HStack { TextField("设备首页的六位配对码",text:$model.code).textFieldStyle(.roundedBorder).frame(maxWidth:240);Button("配对设备") { model.engine.pair(model.code) }.disabled(!model.state.connected || model.code.count != 6) }
                }
            }
            card("语音通道","waveform") {
                readiness("音频驱动",detail:model.state.audio ? "BlackHole 2ch" : "安装后，在输入法中选择 BlackHole 2ch",ready:model.state.audio) {
                    Link(model.state.audio ? "安装说明" : "安装 BlackHole",destination:URL(string:"https://existential.audio/blackhole/")!)
                }
                Divider().overlay(Palette.line)
                readiness("快捷键权限",detail:model.state.permission ? "已授权" : "用于发送语音与回车按键",ready:model.state.permission) {
                    if !model.state.permission { Button(model.permissionChecking ? "正在检查…" : "前往授权") { model.requestKeyboardPermission() }.disabled(model.permissionChecking) }
                }
                Divider().overlay(Palette.line)
                VStack(alignment:.leading,spacing:10) {
                    HStack(spacing:7) {
                        Text("输入法 · 按住说话").font(.system(size:12,weight:.medium))
                        Image(systemName:"questionmark.circle").foregroundStyle(Palette.muted)
                            .help("可对接支持自定义“按住快捷键说话”、能使用 BlackHole 2ch 作为麦克风的输入法。需在输入法中选择 BlackHole，并设置相同的快捷键。目前已实测微信输入法，其他输入法需确认是否接受模拟按键。")
                            .accessibilityLabel("输入法兼容条件")
                    }
                    HStack { TextField("ctrl+option+v",text:$model.shortcut).textFieldStyle(.roundedBorder).font(.system(size:12,design:.monospaced)).frame(width:205);Button("保存快捷键") { model.saveShortcut() } }
                    Text("在输入法中选择 BlackHole 2ch，并设置相同快捷键。").font(.system(size:11)).foregroundStyle(Palette.muted)
                        .help("默认使用左 Control＋左 Option＋V。若发生冲突，可尝试加上 Command，并同步修改输入法设置；无法自动检查所有应用的快捷键。")
                    if Shortcut.hasElevatedConflictRisk(model.shortcut) {
                        Text(Shortcut.guidance(model.shortcut)).font(.system(size:11)).foregroundStyle(Color.orange).fixedSize(horizontal:false,vertical:true)
                    }
                }
            }
            if model.state.powerAvailable && model.state.paired {
                card("待机诊断","bolt.circle") {
                    PowerDiagnosticsView(state:model.state,read:{ model.engine.readPower(reset:$0) },copy:{ report in
                        let pasteboard=NSPasteboard.general;pasteboard.clearContents()
                        model.result=pasteboard.setString(report,forType:.string) ? "诊断报告已复制" : "复制失败，请重试"
                    })
                }
            }
        }
    }
    private func readiness<Action:View>(_ title:String,detail:String,ready:Bool,@ViewBuilder action:()->Action)->some View {
        HStack(spacing:10) {
            Image(systemName:ready ? "checkmark.circle.fill" : "circle.dashed").foregroundStyle(ready ? Palette.accent : .orange)
            VStack(alignment:.leading,spacing:4) { Text(title).font(.system(size:12,weight:.medium));Text(detail).font(.system(size:11)).foregroundStyle(Palette.muted).fixedSize(horizontal:false,vertical:true) }
            Spacer();action().font(.system(size:11)).buttonStyle(.borderless)
        }
    }
    private var device:some View {
        VStack(spacing:16) {
            card("显示","sun.max") {
                if model.settings.appearance != nil {
                    VStack(alignment:.leading,spacing:12) {
                        Text("设备主题").font(.system(size:12,weight:.medium))
                        Picker("设备主题",selection:Binding(get:{model.settings.appearance?.mode ?? 0},set:{model.settings.appearance?.mode=$0})) {
                            Text("暗色").tag(0);Text("亮色").tag(1);Text("定时切换").tag(2)
                        }.pickerStyle(.segmented).labelsHidden()
                        if model.settings.appearance?.mode==2 {
                            HStack(spacing:28) { themeTime("亮色开始",\.light_start);themeTime("暗色开始",\.dark_start) }
                            if model.settings.appearance?.valid==false { Text("两个开始时间不能相同").font(.system(size:10)).foregroundStyle(.orange) }
                        }
                    }.help("按设备本地时间切换，可跨午夜；保存后客户端退出仍会执行。")
                    Divider().overlay(Palette.line)
                } else {
                    Text(model.loaded ? "更新设备固件后，可设置亮色、暗色和定时切换。" : "连接设备后读取主题设置。").font(.system(size:11)).foregroundStyle(Palette.muted)
                }
                number("屏幕亮度",value:$model.settings.brightness,range:10...100,unit:"%")
                number("自动息屏",value:$model.settings.screen_seconds,range:3...60,unit:"秒",help:"3–60 秒。按住上键可唤醒并开始语音输入。")
                if model.settings.screen_seconds<10 && model.settings.screen_seconds_min != 3 { Text("3–9 秒需要新版设备固件；目前的实测固件仅支持 10 秒起。").font(.system(size:10)).foregroundStyle(.orange) }
            }
            card("声音","speaker.wave.2") {
                number("设备音量",value:$model.settings.volume,range:0...100,unit:"%")
                number("麦克风增益",value:$model.settings.mic_gain,range:0...42,unit:"dB",help:"控制 FoloToy 麦克风录音声音的放大程度，不影响扬声器音量。\n\n通常保持默认 30 dB。声音太小、离设备稍远时，可每次增加 2–3 dB；声音破裂、失真或背景噪声过大时，可每次降低 2–3 dB，再试说一句。\n\n增益也会放大噪声，不能代替降噪；不是越高识别越准。")
            }
        }
    }
    private func themeTime(_ title:String,_ field:WritableKeyPath<AppearanceSettings,Int>)->some View {
        DatePicker(title,selection:Binding(get:{
            let minutes=(model.settings.appearance ?? AppearanceSettings())[keyPath:field]
            return Calendar.current.date(from:DateComponents(year:2001,month:1,day:1,hour:minutes/60,minute:minutes%60)) ?? Date()
        },set:{ date in
            var appearance=model.settings.appearance ?? AppearanceSettings()
            let parts=Calendar.current.dateComponents([.hour,.minute],from:date)
            appearance[keyPath:field]=(parts.hour ?? 0)*60+(parts.minute ?? 0)
            model.settings.appearance=appearance
        }),displayedComponents:.hourAndMinute)
            .datePickerStyle(.field).environment(\.locale,Locale(identifier:"zh_CN")).font(.system(size:12)).fixedSize()
    }
    private var pomodoro:some View {
        VStack(spacing:16) {
            card("时长设置","timer") {
                HStack(alignment:.center,spacing:24) {
                    durationSummary(model.settings.focus_minutes,label:"专注",symbol:"leaf",color:Palette.accent)
                    Image(systemName:"arrow.right").font(.system(size:18,weight:.light)).foregroundStyle(Palette.muted)
                    durationSummary(model.settings.break_minutes,label:"休息",symbol:"cup.and.saucer",color:Color(red:0.85,green:0.76,blue:0.55))
                }.padding(.vertical,8)
                number("专注时长",value:$model.settings.focus_minutes,range:1...180,unit:"分钟")
                number("休息时长",value:$model.settings.break_minutes,range:1...60,unit:"分钟")
            }
        }
    }
    private func durationSummary(_ minutes:Int,label:String,symbol:String,color:Color)->some View {
        VStack(alignment:.leading,spacing:8) {
            Label(label,systemImage:symbol).font(.system(size:12,weight:.medium)).foregroundStyle(color)
            HStack(alignment:.firstTextBaseline,spacing:5) { Text("\(minutes)").font(.system(size:48,weight:.light,design:.rounded)).monospacedDigit();Text("分钟").font(.system(size:12)).foregroundStyle(Palette.muted) }
        }.frame(maxWidth:.infinity,alignment:.leading)
    }
    private var weather:some View {
        VStack(spacing:16) {
            card("首页天气","cloud.sun") {
                HStack { Image(systemName:"location").foregroundStyle(Palette.accent);Text(model.settings.city.isEmpty ? "尚未选择城市" : model.settings.city).font(.system(size:22,weight:.medium));Spacer() }.padding(.vertical,8)
                HStack { TextField("搜索城市，支持中文或拼音",text:$model.cityQuery).textFieldStyle(.roundedBorder).onSubmit { model.findCity() };Button(model.searching ? "正在搜索…" : "搜索") { model.findCity() }.disabled(model.searching) }
                ForEach(model.cities) { city in Button { model.settings.city=city.name;model.settings.latitude=city.latitude;model.settings.longitude=city.longitude;model.settings.weather_enabled=true;model.cities=[];model.result="已选择城市，保存后生效" } label: { HStack { Text(city.description);Spacer();Image(systemName:"plus.circle") } }.buttonStyle(.plain).font(.system(size:12)).padding(.vertical,4) }
            }

        }
    }
    private var radio:some View {
        VStack(alignment:.leading,spacing:16) {
            Text("从电台库加入「我的电台」，拖动调整顺序。设备按这个顺序切换。").font(.system(size:12)).foregroundStyle(Palette.muted).lineSpacing(4)
            if model.settings.radio_list_version != 1 { Label("请更新设备固件后管理电台列表",systemImage:"arrow.down.circle").font(.system(size:11)).foregroundStyle(.orange) }
            HStack(alignment:.top,spacing:12) {
                card("电台库","antenna.radiowaves.left.and.right") {
                    Picker("来源",selection:$model.radioSource) { Text("推荐").tag(0);Text("搜索").tag(1);Text("自定义").tag(2) }.pickerStyle(.segmented).labelsHidden()
                    radioSources
                }
                card("我的电台 · \(model.radioStations.count)/\(model.radioCapacity)","list.bullet") {
                    Text("拖动换序 · 上下箭头也可调整").font(.system(size:10)).foregroundStyle(Palette.muted)
                    if model.radioLoading { ProgressView("正在同步设备…").font(.system(size:11)) }
                    ScrollView {
                        LazyVStack(spacing:6) {
                            ForEach(model.radioStations) { station in selectedStation(station) }
                            if model.radioStations.isEmpty { Text("从左侧加入电台").font(.system(size:12)).foregroundStyle(Palette.muted).padding(.vertical,50) }
                        }
                    }.frame(height:330)
                }
            }
        }
    }
    @ViewBuilder private var radioSources:some View {
        if model.radioSource==2 {
            VStack(alignment:.leading,spacing:12) {
                TextField("电台名称",text:$model.customName).textFieldStyle(.roundedBorder)
                TextField("HTTP / HTTPS 音频流地址",text:$model.customURL).textFieldStyle(.roundedBorder).font(.system(size:11,design:.monospaced))
                Button(model.radioChecking ? "正在检测…" : "检测并添加") { model.checkCustomStation() }.disabled(model.radioChecking)
                Spacer()
            }.frame(height:363)
        } else {
            VStack(alignment:.leading,spacing:10) {
                if model.radioSource==0 { TextField("搜索推荐电台",text:$model.radioQuery).textFieldStyle(.roundedBorder) }
                if model.radioSource==1 {
                    HStack(spacing:8) {
                        TextField("搜索电台",text:$model.radioQuery).textFieldStyle(.roundedBorder).frame(minWidth:80,maxWidth:.infinity).onSubmit { model.findStations() }
                        Toggle("中文",isOn:$model.radioChineseOnly).toggleStyle(.checkbox).font(.system(size:11)).fixedSize()
                        Button("搜索") { model.findStations() }.fixedSize().disabled(model.radioSearching)
                    }
                }
                ScrollView {
                    LazyVStack(spacing:7) {
                        ForEach(model.radioSource==0 ? model.filteredPresets : model.radioResults) { station in libraryStation(station) }
                        if model.radioSource==1 {
                            if model.radioSearching { ProgressView().controlSize(.small).padding(15) }
                            else if model.radioHasMore { Button("加载更多") { model.findStations(more:true) }.font(.system(size:11)) }
                            else if model.radioResults.isEmpty { Text("输入名称搜索，留空可浏览热门电台").font(.system(size:11)).foregroundStyle(Palette.muted).padding(.vertical,40) }
                        }
                    }
                }.frame(height:model.radioSource==0 ? 330 : 296)
                if model.radioSource==1 { Link("电台目录由 Radio Browser 提供",destination:URL(string:"https://www.radio-browser.info/")!).font(.system(size:10)) }
            }
        }
    }
    private func libraryStation(_ station:RadioStation)->some View {
        let added=model.radioStations.contains(where:{$0.url==station.url})
        return Button { model.addStation(station) } label: {
            HStack(spacing:8) { Image(systemName:"radio").foregroundStyle(Palette.muted);Text(station.name).lineLimit(2);Spacer(minLength:2);Image(systemName:added ? "checkmark" : "plus").foregroundStyle(Palette.accent) }
                .font(.system(size:11)).padding(10).frame(maxWidth:.infinity,alignment:.leading).contentShape(Rectangle()).background(Palette.background,in:RoundedRectangle(cornerRadius:7))
        }.buttonStyle(.plain).disabled(added || model.radioLoading).help(station.url)
    }
    private func selectedStation(_ station:RadioStation)->some View {
        HStack(spacing:7) {
            Image(systemName:"line.3.horizontal").foregroundStyle(Palette.muted)
            VStack(alignment:.leading,spacing:3) {
                Text(station.name).font(.system(size:11)).lineLimit(2)
                if let check=model.radioChecks[station.url] { Text(check).font(.system(size:9)).foregroundStyle(Palette.accent) }
            }.frame(maxWidth:.infinity,alignment:.leading)
            VStack(spacing:5) {
                Button { model.moveStation(station,by:-1) } label: { Image(systemName:"chevron.up") }.help("上移电台")
                Button { model.moveStation(station,by:1) } label: { Image(systemName:"chevron.down") }.help("下移电台")
            }.font(.system(size:8))
            Button { model.radioStations.removeAll { $0.id==station.id };model.result="已移除，保存后生效" } label: { Image(systemName:"minus.circle") }.help("移除电台")
        }.buttonStyle(.borderless).padding(10).background(Palette.background,in:RoundedRectangle(cornerRadius:7)).contentShape(Rectangle())
            .onDrag { model.draggingStation=station.id;return NSItemProvider(object:station.id as NSString) }
            .onDrop(of:[UTType.text],delegate:StationDropDelegate(model:model,target:station.id))
            .disabled(model.radioLoading)
    }
    private func number(_ title:String,value:Binding<Int>,range:ClosedRange<Int>,unit:String,help:String?=nil)->some View {
        VStack(alignment:.leading,spacing:9) {
            HStack { VStack(alignment:.leading,spacing:4) {
                HStack(spacing:6) { Text(title).font(.system(size:12,weight:.medium));if let explanation=help { Image(systemName:"questionmark.circle").font(.system(size:12)).foregroundStyle(Palette.muted).help(explanation).accessibilityLabel("\(title)说明：\(explanation)") } }
            };Spacer();Stepper(value:value,in:range) { Text("\(value.wrappedValue) \(unit)").font(.system(size:12,weight:.medium,design:.monospaced)).monospacedDigit().frame(width:88,alignment:.trailing) }.fixedSize() }
            Slider(value:Binding(get:{Double(value.wrappedValue)},set:{value.wrappedValue=Int($0.rounded())}),in:Double(range.lowerBound)...Double(range.upperBound),step:1).controlSize(.small).accessibilityLabel(title)
        }.padding(.bottom,5)
    }
    private var footer:some View {
        HStack(spacing:12) {
            if model.saving { ProgressView().controlSize(.small) }
            Text(model.result).font(.system(size:11)).foregroundStyle(Palette.muted).lineLimit(2).textSelection(.enabled)
            Spacer(minLength:6)
            if model.tab != 0 && model.tab != 5 {
                Button("重新读取") { model.reloadCurrent() }.buttonStyle(.borderless).disabled(!model.state.paired || model.saving)
                Button(model.saving ? "正在保存" : "保存到设备") { model.save() }.buttonStyle(.borderedProminent).tint(Palette.accent).foregroundStyle(Palette.background).disabled(!model.loaded || !model.state.paired || !model.state.connected || model.saving || model.state.recording || model.radioLoading || model.radioChecking || (model.tab==4 && !model.radioLoaded))
            }
        }.padding(.horizontal,28).padding(.vertical,16).frame(minHeight:26).background(Palette.sidebar)
    }
}

struct StationDropDelegate:DropDelegate {
    let model:AppModel,target:String
    func dropEntered(info:DropInfo) { if !model.radioLoading,let source=model.draggingStation { model.moveStation(source,to:target) } }
    func dropUpdated(info:DropInfo)->DropProposal? { DropProposal(operation:.move) }
    func performDrop(info:DropInfo)->Bool { model.draggingStation=nil;return true }
}
