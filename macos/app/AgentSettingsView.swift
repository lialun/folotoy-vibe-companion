import SwiftUI
import AppKit
struct AgentSettingsView:View {
    @ObservedObject var center:AgentCenter
    let preview:Bool
    var body:some View {
        VStack(alignment:.leading,spacing:18) {
            HStack {
                Toggle("设备提醒",isOn:$center.enabled).toggleStyle(.switch)
                Spacer()
                Button("测试提醒") { if !preview { center.test() } }.disabled(!center.enabled)
                Button { center.refresh() } label: { Image(systemName:"arrow.clockwise") }.disabled(center.busy).help("重新检测 Agent")
            }
            ForEach(center.integrations) { row in
                VStack(alignment:.leading,spacing:10) {
                    HStack {
                        VStack(alignment:.leading,spacing:5) {
                            Text(row.agent.title).font(.system(size:15,weight:.semibold))
                            Text(row.agent.coverage).font(.system(size:11)).foregroundStyle(.secondary)
                        }
                        Spacer()
                        if row.agent == .dsh {
                            Button("准备插件") { center.dshPathCopied=false;center.change(row,install:true) }.disabled(center.busy || !row.detected || preview)
                        } else {
                            Button(row.installed ? "移除集成" : "安装集成") { center.change(row,install:!row.installed) }.disabled(center.busy || !row.detected || preview)
                        }
                    }
                    HStack(spacing:6) {
                        Circle().fill(row.installed ? Color.green : Color.gray).frame(width:5,height:5)
                        Text(row.installed ? "已安装" : row.detected ? "已检测到" : "未检测到").font(.system(size:11))
                        Spacer()
                        if row.installed {
                            Button("配置说明") { center.installationNotice=AgentInstallNotice(agent:row.agent) }
                                .buttonStyle(.borderless).font(.system(size:11))
                        }
                    }
                    if row.agent == .dsh,row.detected {
                        let directory=PairingStore.directory.appendingPathComponent("agents/vibe-companion-dsh")
                        let prepared=FileManager.default.fileExists(atPath:directory.appendingPathComponent("package.json").path)
                        HStack {
                            Spacer()
                            Button(center.dshPathCopied ? "已复制路径" : "复制插件路径") {
                                let pasteboard=NSPasteboard.general
                                pasteboard.clearContents()
                                center.dshPathCopied=pasteboard.setString(directory.path,forType:.string)
                                if !center.dshPathCopied { center.message="复制失败，请重试" }
                            }.font(.system(size:11)).buttonStyle(.borderless)
                                .disabled(center.busy || !prepared || preview)
                                .help(prepared ? "复制完整目录路径，粘贴到 DSH 的本地插件安装入口" : "请先准备插件")
                            Button("显示插件") { NSWorkspace.shared.activateFileViewerSelecting([directory]) }.font(.system(size:11)).buttonStyle(.borderless)
                                .disabled(center.busy || !prepared || preview)
                            if let app=row.application { Button("打开 DSH") { NSWorkspace.shared.openApplication(at:app,configuration:NSWorkspace.OpenConfiguration()) }.font(.system(size:11)).buttonStyle(.borderless) }
                        }
                    }
                }.padding(16).background(Color.white.opacity(0.045),in:RoundedRectangle(cornerRadius:12))
            }
            if center.busy { ProgressView().controlSize(.small) }
            if !center.message.isEmpty { Text(center.message).font(.system(size:12)).foregroundStyle(.secondary).textSelection(.enabled) }
            if !center.recent.isEmpty {
                Divider()
                Text("最近事件").font(.system(size:12,weight:.semibold))
                ForEach(center.recent.prefix(6)) { event in
                    HStack { Text(event.agent.title);Text(event.kind.title).foregroundStyle(.secondary);Text(event.project).foregroundStyle(.secondary).lineLimit(1);Spacer();Text(Date(timeIntervalSince1970:event.created),style:.time).monospacedDigit() }.font(.system(size:11))
                }
            }
        }.sheet(item:$center.installationNotice) { notice in
            VStack(alignment:.leading,spacing:20) {
                Text(notice.title).font(.system(size:19,weight:.semibold))
                Text(notice.instructions).font(.system(size:13)).lineSpacing(6)
                    .fixedSize(horizontal:false,vertical:true).textSelection(.enabled)
                HStack {
                    Spacer()
                    Button("知道了") { center.installationNotice=nil }
                        .buttonStyle(.borderedProminent).keyboardShortcut(.defaultAction)
                }
            }.padding(28).frame(width:620,alignment:.leading)
                .background(Palette.surface).foregroundStyle(Palette.text).tint(Palette.accent).preferredColorScheme(.dark)
        }
    }
}
