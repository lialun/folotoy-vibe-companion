import SwiftUI
import AppKit

struct DeviceConnectionView:View {
    @ObservedObject var model:AppModel
    @State private var switching:String?
    @State private var confirmSwitch=false
    @State private var confirmUnpair=false
    var body:some View {
        VStack(alignment:.leading,spacing:14) {
            HStack {
                BrandMark(size:40)
                Text(model.state.message).font(.system(size:15,weight:.medium))
                Spacer()
                Button { model.retryConnection() } label: { Label("刷新",systemImage:"arrow.clockwise") }
                    .buttonStyle(.borderless).font(.system(size:11))
            }
            if model.state.devices.isEmpty {
                Text("打开设备和蓝牙，或连接同一局域网。").font(.system(size:11)).foregroundStyle(Palette.muted)
                Button("打开局域网权限") {
                    NSWorkspace.shared.open(URL(string:"x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork")!)
                }.buttonStyle(.bordered).controlSize(.small)
            } else {
                ForEach(model.state.devices) { device in
                    Button {
                        if device.id != model.state.device && model.hasUnsavedDeviceEdits { switching=device.id;confirmSwitch=true }
                        else { model.selectDevice(device.id) }
                    } label: {
                        HStack(spacing:10) {
                            Image(systemName:device.id==model.state.device ? "checkmark.circle.fill" : "circle")
                                .foregroundStyle(device.id==model.state.device ? Palette.accent : Palette.muted)
                            VStack(alignment:.leading,spacing:4) {
                                Text(device.title).font(.system(size:12,weight:.medium))
                                Text([device.hasWiFi ? "Wi-Fi" : "",device.hasBLE ? "蓝牙" : ""].filter{!$0.isEmpty}.joined(separator:" · ")).font(.system(size:10,design:.monospaced)).foregroundStyle(Palette.muted)
                            }
                            Spacer()
                            Text(device.id==model.state.device && model.state.connected ? "已连接" : device.id==model.state.device && model.state.connectionRequestActive ? (model.state.waitingForConfirmation ? "待确认" : "连接中") : "连接")
                                .font(.system(size:11,weight:.medium)).foregroundStyle(Palette.accent)
                        }.padding(10).frame(maxWidth:.infinity,alignment:.leading).contentShape(Rectangle())
                            .background(device.id==model.state.device ? Palette.accent.opacity(0.07) : Palette.background.opacity(0.4),in:RoundedRectangle(cornerRadius:8))
                    }.buttonStyle(.plain).disabled(device.id==model.state.device && (model.state.connected || model.state.connectionRequestActive))
                }
            }
            if model.state.connected {
                Text([model.state.wifiConnected ? "Wi-Fi 已连接" : "",model.state.bleConnected ? "蓝牙已连接" : ""].filter{!$0.isEmpty}.joined(separator:" · "))
                    .font(.system(size:11)).foregroundStyle(Palette.muted)
            }
            if !model.state.bluetoothStatus.isEmpty {
                Text(model.state.bluetoothStatus).font(.system(size:11)).foregroundStyle(Palette.muted)
            }
            if model.state.bluetoothPairingRepairRequired {
                Text("在系统蓝牙中忽略 Vibe-\(model.state.device)，然后重试。")
                    .font(.system(size:11)).foregroundStyle(Palette.muted)
                HStack {
                    Button("打开蓝牙设置") { NSWorkspace.shared.open(URL(string:"x-apple.systempreferences:com.apple.Bluetooth")!) }
                    Button("重试蓝牙") { model.retryBluetooth() }
                }.controlSize(.small).buttonStyle(.bordered)
            }
            if model.state.connectionRequestActive {
                Button("取消连接") { model.engine.cancelConnection() }.controlSize(.small).buttonStyle(.bordered)
            }
            if model.state.connected {
                HStack {
                    Button("断开连接") { model.engine.disconnectDevice() }
                    Button("解除配对",role:.destructive) { confirmUnpair=true }
                }.controlSize(.small).buttonStyle(.bordered)
            } else if model.state.paused {
                Button("重新连接") { model.selectDevice(model.state.device) }.controlSize(.small).buttonStyle(.bordered)
            }
        }.alert("切换设备？",isPresented:$confirmSwitch) {
            Button("取消",role:.cancel) { switching=nil }
            Button("切换",role:.destructive) { if let id=switching { model.selectDevice(id) };switching=nil }
        } message: { Text("当前设备未保存的修改将被丢弃。") }
            .alert("解除与这台设备的配对？",isPresented:$confirmUnpair) {
                Button("取消",role:.cancel) {}
                Button("解除配对",role:.destructive) { model.engine.unpairDevice() }
            } message: { Text("下次连接需在设备上确认。Wi-Fi 设置保留。") }
    }
}
