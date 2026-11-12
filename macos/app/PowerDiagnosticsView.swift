import SwiftUI

struct PowerDiagnosticsView:View {
    let state:ConnectionState
    let read:(Bool)->Void
    var body:some View {
        VStack(alignment:.leading,spacing:16) {
            HStack {
                if let snapshot=state.powerSnapshot {
                    Text("读取于 \(snapshot.received.formatted(date:.omitted,time:.standard))")
                        .font(.system(size:10)).foregroundStyle(Palette.muted)
                }
                Spacer()
                Button(state.powerLoading ? "正在读取…" : "读取统计") { read(false) }
                    .disabled(unavailable)
                Button("重新计时") { read(true) }.disabled(unavailable)
                    .help("只重置本轮睡眠统计；电源锁时长仍从开机累计。")
            }.font(.system(size:11))
            if !state.powerError.isEmpty {
                Text(state.powerError).font(.system(size:12)).foregroundStyle(.orange)
            }
            if let snapshot=state.powerSnapshot {
                content(snapshot)
            } else {
                Text("拔掉 USB，让设备息屏运行 5–10 分钟，再读取统计。通过当前连接读取，无需接线。")
                    .font(.system(size:12)).foregroundStyle(Palette.muted).fixedSize(horizontal:false,vertical:true)
            }
        }
    }
    private var unavailable:Bool { state.powerLoading || !state.connected || !state.owned || state.recording }
    private func content(_ s:PowerSnapshot)->some View {
        VStack(alignment:.leading,spacing:16) {
            if s.usb==true {
                Label("USB 主机已连接，会阻止自动浅睡眠。拔线后重新计时再测试。",systemImage:"exclamationmark.triangle")
                    .font(.system(size:11)).foregroundStyle(.orange).fixedSize(horizontal:false,vertical:true)
            }
            HStack(alignment:.top,spacing:18) {
                metric("息屏浅睡占比",s.ratioText,accent:true)
                metric("息屏时长",PowerDiagnostics.duration(s.blankMS))
                metric("息屏未浅睡",PowerDiagnostics.duration(s.blankAwakeMS))
            }
            if let ratio=s.blankRatio {
                GeometryReader { proxy in
                    Capsule().fill(Palette.line)
                    Capsule().fill(Palette.accent).frame(width:proxy.size.width*ratio)
                }.frame(height:4).accessibilityLabel("息屏浅睡占比 \(s.ratioText)")
            }
            LazyVGrid(columns:[GridItem(.flexible(),alignment:.leading),GridItem(.flexible(),alignment:.leading)],alignment:.leading,spacing:10) {
                detail("本轮统计",PowerDiagnostics.duration(s.elapsedMS))
                detail("亮屏时长",PowerDiagnostics.duration(s.screenOnMS))
                detail("浅睡次数",s.sleeps.map{"\($0) 次"} ?? "—")
                detail("进入被拒绝",s.rejects.map{"\($0) 次"} ?? "—")
                detail("平均单次浅睡",PowerDiagnostics.milliseconds(s.averageMS))
                detail("最长单次浅睡",PowerDiagnostics.milliseconds(s.longestMS))
                detail("电池读数",s.batteryText)
                detail("闲置按键唤醒",s.keyWake.map{$0 ? "已启用" : "扫描中或已回退"} ?? "未知")
            }
            Divider().overlay(Palette.line)
            memory(s.heap)
            Divider().overlay(Palette.line)
            lockTable(s)
            Divider().overlay(Palette.line)
            LazyVGrid(columns:[GridItem(.flexible(),alignment:.leading),GridItem(.flexible(),alignment:.leading)],alignment:.leading,spacing:10) {
                detail("全部浅睡耗时",PowerDiagnostics.duration(s.sleepMS))
                    .help("本轮全部成功浅睡调用的耗时，含进入／退出开销；息屏浅睡是其中屏幕关闭的部分。")
                detail("USB 主机",s.usb.map{$0 ? "已连接" : "未连接"} ?? "未知")
                detail("电源锁明细",s.lockReportStatus)
            }
            Text("读取会唤醒 CPU／Wi-Fi，当前持有不等于持续阻塞。浅睡时长含进入／退出开销；实际耗电仍需测量电流。")
                .font(.system(size:10)).foregroundStyle(Palette.muted).fixedSize(horizontal:false,vertical:true)
        }
    }
    private func memory(_ heap:HeapSnapshot?)->some View {
        VStack(alignment:.leading,spacing:10) {
            HStack(spacing:6) {
                Text("运行内存（动态堆）").font(.system(size:12,weight:.medium))
                Image(systemName:"questionmark.circle").font(.system(size:11)).foregroundStyle(Palette.muted)
                    .help("8 位动态堆，供任务、音频、网络和界面分配；不含固件静态区。最大连续空闲块决定单次大块分配的空间。开机最低余量是各堆区域历史最低值之和，重新计时不会清零。1 KiB = 1024 字节。")
                Spacer()
                Text("读取前快照").font(.system(size:10)).foregroundStyle(Palette.muted)
            }
            if let heap=heap {
                HStack {
                    Text("已用 \(PowerDiagnostics.bytes(heap.used))").foregroundStyle(Palette.text)
                    Spacer()
                    Text("可用 \(PowerDiagnostics.bytes(heap.free))").foregroundStyle(Palette.accent)
                }.font(.system(size:12)).monospacedDigit()
                if let ratio=heap.usedRatio {
                    GeometryReader { proxy in
                        Capsule().fill(Palette.line)
                        Capsule().fill(Palette.muted.opacity(0.7)).frame(width:proxy.size.width*ratio)
                    }.frame(height:4).accessibilityLabel("堆内存已用 \(Int(ratio*100))%")
                }
                HStack(spacing:20) {
                    detail("最大连续空闲块",PowerDiagnostics.bytes(heap.largest))
                    detail("开机最低余量",PowerDiagnostics.bytes(heap.minimum))
                }
            } else {
                Text("当前固件未提供内存数据").font(.system(size:11)).foregroundStyle(Palette.muted)
            }
        }
    }
    private func metric(_ title:String,_ value:String,accent:Bool=false)->some View {
        VStack(alignment:.leading,spacing:6) {
            Text(title).font(.system(size:11)).foregroundStyle(Palette.muted)
            Text(value).font(.system(size:accent ? 27 : 20,weight:.medium)).monospacedDigit()
                .foregroundStyle(accent ? Palette.accent : Palette.text).lineLimit(1).minimumScaleFactor(0.7)
        }.frame(maxWidth:.infinity,alignment:.leading)
    }
    private func detail(_ title:String,_ value:String)->some View {
        HStack(spacing:8) {
            Text(title).foregroundStyle(Palette.muted)
            Spacer(minLength:4)
            Text(value).monospacedDigit().foregroundStyle(Palette.text)
        }.font(.system(size:11)).padding(.trailing,12)
    }
    private func lockTable(_ s:PowerSnapshot)->some View {
        VStack(alignment:.leading,spacing:10) {
            HStack {
                Text("电源锁").font(.system(size:12,weight:.medium))
                Spacer()
                Text("读取时状态 · 时长从开机累计").font(.system(size:10)).foregroundStyle(Palette.muted)
            }
            if s.locks.isEmpty {
                Text("设备未提供可识别的电源锁明细").font(.system(size:11)).foregroundStyle(Palette.muted)
            } else {
                HStack {
                    Text("组件").frame(maxWidth:.infinity,alignment:.leading)
                    Text("约束").frame(width:94,alignment:.leading)
                    Text("当前").frame(width:72,alignment:.leading)
                    Text("累计持有").frame(width:116,alignment:.trailing)
                }.font(.system(size:10)).foregroundStyle(Palette.muted)
                ForEach(s.locks) { group in
                    HStack {
                        HStack(spacing:5) {
                            Text(group.title)
                            if group.locks.count>1 { Text("×\(group.locks.count)").foregroundStyle(Palette.muted) }
                        }.frame(maxWidth:.infinity,alignment:.leading)
                        Text(group.constraint).foregroundStyle(Palette.muted).frame(width:94,alignment:.leading)
                        Text(group.status).foregroundStyle(group.activeCount>0 ? Color.orange : Palette.muted).frame(width:72,alignment:.leading)
                        Text(group.heldText).monospacedDigit().frame(width:116,alignment:.trailing)
                    }.font(.system(size:11)).help(groupHelp(group))
                }
            }
            if s.locks.contains(where:{$0.locks.count>1}) {
                Text("同名同类锁合并显示；累计时间按每把锁列出，不相加。音频 I²S 的两把锁分别属于录音和播放通道。")
                    .font(.system(size:10)).foregroundStyle(Palette.muted).fixedSize(horizontal:false,vertical:true)
            }
            if s.locksTruncated || s.unparsedLocks {
                Text(s.locksTruncated ? "设备明细已截断，锁列表可能不完整。" : "部分记录未识别，可复制报告查看原始内容。")
                    .font(.system(size:11)).foregroundStyle(.orange)
            }
        }
    }
    private func groupHelp(_ group:PowerLockGroup)->String {
        let exact=group.locks.map{"active=\($0.active)，held_ms=\(String(format:"%.0f",$0.heldMS))"}.joined(separator:"\n")
        return "\(group.name) · \(group.type)\n\(exact)\nactive 是嵌套持有计数；0 表示已释放。"
    }
}
