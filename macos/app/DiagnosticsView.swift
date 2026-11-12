import SwiftUI

struct DeviceStatusView:View {
    @ObservedObject var center:DiagnosticCenter
    let state:ConnectionState
    let readPower:(Bool)->Void
    let readRecords:()->Void
    let copy:(String)->Void
    var body:some View {
        VStack(alignment:.leading,spacing:18) {
            HStack {
                Text("运行统计").font(.system(size:13,weight:.medium))
                Spacer()
                Button("复制报告") { copy(center.report(power:state.powerSnapshot)) }
                    .font(.system(size:11)).disabled(state.powerSnapshot==nil && center.records.isEmpty)
                    .help("复制运行统计与事件记录，供反馈问题或交给大模型分析")
            }
            if state.powerAvailable || state.powerSnapshot != nil {
                PowerDiagnosticsView(state:state,read:readPower)
            } else {
                Text(state.connected ? "当前固件未提供运行统计" : "连接设备后可读取睡眠、内存和电池统计")
                    .font(.system(size:11)).foregroundStyle(Palette.muted)
            }
            Divider().overlay(Palette.line)
            DiagnosticsView(center:center,state:state,read:readRecords)
        }
    }
}

struct DiagnosticsView:View {
    @ObservedObject var center:DiagnosticCenter
    let state:ConnectionState
    let read:()->Void
    var body:some View {
        VStack(alignment:.leading,spacing:18) {
            HStack(alignment:.center) {
                VStack(alignment:.leading,spacing:5) {
                    Text("事件记录").font(.system(size:13,weight:.medium))
                    Text("\(center.records.filter{$0.level=="error"}.count) 条异常 · \(center.records.count) 条记录")
                        .font(.system(size:11)).foregroundStyle(Palette.muted)
                }
                Spacer()
                Button(state.diagnosticsLoading ? "正在读取…" : "读取设备记录",action:read)
                    .disabled(!state.connected || !state.owned || !state.diagnosticsAvailable || state.diagnosticsLoading || state.recording)
            }.font(.system(size:11))
            if !state.diagnosticsError.isEmpty { notice(state.diagnosticsError) }
            if !center.storageError.isEmpty { notice(center.storageError) }
            if state.connected && !state.diagnosticsAvailable {
                notice("当前固件不支持异常上报；电脑端记录仍会保存。")
            }
            if center.records.isEmpty {
                VStack(spacing:12) {
                    Text("暂无记录").font(.system(size:13,weight:.medium))
                    Text("设备与电脑端的异常会自动保存在这里。")
                        .font(.system(size:12)).foregroundStyle(Palette.muted)
                }.frame(maxWidth:.infinity).padding(.vertical,20)
            } else {
                LazyVStack(alignment:.leading,spacing:0) {
                    ForEach(center.records.prefix(center.visibleCount)) { row in
                        HStack(alignment:.top,spacing:12) {
                            Image(systemName:row.level=="error" ? "exclamationmark.circle.fill" : row.level=="warning" ? "exclamationmark.triangle" : "power")
                                .font(.system(size:14)).foregroundStyle(row.level=="error" ? Color.orange : Palette.muted).frame(width:18).padding(.top,2)
                            VStack(alignment:.leading,spacing:7) {
                                HStack(alignment:.firstTextBaseline) {
                                    Text(row.title).font(.system(size:13,weight:.medium))
                                    Spacer(minLength:10)
                                    Text(row.received.formatted(date:.abbreviated,time:.standard)).font(.system(size:10)).foregroundStyle(Palette.muted)
                                }
                                if !row.details.isEmpty { Text(row.details).font(.system(size:11)).foregroundStyle(Palette.muted) }
                                HStack(spacing:8) {
                                    Text(row.code.rawValue)
                                    if !row.link.isEmpty { Text(row.link=="wifi" ? "Wi-Fi" : "BLE") }
                                    if let stream=row.numbers["stream"] { Text("stream \(stream)") }
                                }.font(.system(size:10,design:.monospaced)).foregroundStyle(Palette.muted).textSelection(.enabled)
                            }
                        }.padding(.vertical,15)
                        Divider().overlay(Palette.line)
                    }
                }
                if center.visibleCount<center.records.count { Button("显示更多") { center.visibleCount+=50 }.font(.system(size:11)) }
            }
            Text("本机保留最近 500 条；设备离线暂存最近 8 条，断电不保留。")
                .font(.system(size:10)).foregroundStyle(Palette.muted).fixedSize(horizontal:false,vertical:true)
                .help("报告不含密码、配对令牌、电台链接、语音或对话正文。记录时间为电脑收到的时间。")
        }
    }
    private func notice(_ text:String)->some View {
        Label(text,systemImage:"exclamationmark.circle").font(.system(size:11)).foregroundStyle(.orange).fixedSize(horizontal:false,vertical:true)
    }
}
