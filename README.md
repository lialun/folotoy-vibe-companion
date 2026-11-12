**简体中文** · [English](README.en.md)

# FoloToy Vibe Companion · Vibe 伴侣

**语音输入、AI 提醒，你的 Vibe Coding 伴侣。**

把 FoloToy AI Passport 变成无线语音输入设备和 Agent 提醒器。
按住按键向电脑输入文字，Agent 完成任务或需要你操作时亮屏响铃；
天气、番茄钟和收音机，也在手边。

**目前桌面端仅支持 macOS 13+（Apple Silicon / Intel），暂不支持 Windows。**

![Vibe 伴侣：无线语音输入、专注计时与 Agent 提醒](assets/brand/vibe-companion-hero.png)

[安装固件](https://ai-passport.folotoy.cn/plays/2174/) · [下载 Mac 客户端](https://github.com/lialun/folotoy-vibe-companion/releases/latest) ·
[使用指南](docs/vibe/README.zh_CN.md) · [Mac 安装说明](docs/vibe/install.zh_CN.md) ·
[接入 Agent](docs/vibe/agents.zh_CN.md) · [设计规范](docs/vibe/DESIGN.zh_CN.md)

## 能做什么

| 功能 | 使用体验 |
| --- | --- |
| **随手语音输入** | 按住上键说话，松开结束，短按下键回车。笔记本合盖、内置麦克风不方便使用时，也能继续输入；电脑需保持唤醒。 |
| **AI完成通知** | Agent 完成、等待审批、等待回答或出错时，按接入能力亮屏并响铃。紧凑提醒 5 秒后消失；按任意键关闭提醒，同时照常执行该按键的功能。 |
| **番茄工作法** | 一个键开始、暂停、继续番茄钟。结束后停在下一阶段，等你主动开始；计时期间照常使用语音。 |
| **抬眼看天气** | 当前天气、未来六小时温度与降雨、空气与风力、三日预报。手动翻页，支持亮色、暗色和定时切换。 |
| **听自己选的电台** | 在设备上收听网络电台，在桌面端搜索、检测、添加和排序。说话时让出声音通道，结束后重新连接收听。 |
| **复杂设置交给电脑** | 调节亮度、音量、主题、番茄时长和天气城市。客户端常驻菜单栏，关掉窗口后不占 Dock。 |

![Vibe 伴侣的天气、番茄钟、收音机和 Agent 提醒](assets/brand/vibe-companion-screens.png)

下方功能区一次运行天气、番茄钟或收音机中的一个，顶部始终保留语音状态。
右侧窄栏随页面说明三个实体键的作用。

## 桌面端，把复杂设置留给大屏幕

原生 macOS App 集中管理设备设置、电台列表和 Agent 接入。
搜索电台、检测自定义音频流、拖动调整播放顺序，再保存到设备。
关闭窗口后继续常驻菜单栏，不占 Dock；需要时再打开。

![Vibe 伴侣 macOS 客户端：电台库与我的电台](assets/brand/vibe-companion-desktop.png)

## 支持的 Agent

**Codex · Claude Code · OpenCode · DeepSeek Harness · ZCode · Gemini CLI · Qwen Code**

桌面端检测本机安装情况，通过 Hook 或插件接收事件，再把提醒发到设备，
不需要额外注册推送服务。各 Agent、版本及使用入口的覆盖范围不同，
本地 Hook 不会自动覆盖云端／远程会话。具体事件、安装和验证方式见
[Agent 接入文档](docs/vibe/agents.zh_CN.md)。

![Vibe 伴侣 macOS 客户端：Agent 检测与集成管理](assets/brand/vibe-companion-desktop-agents.png)

## 开始使用

你需要 **FoloToy AI Passport**、**macOS 13 或更高版本**，以及可以互通的局域网。
Mac 安装包同时支持 **Apple Silicon 和 Intel**。语音输入另需安装
[BlackHole 2ch](https://existential.audio/blackhole/) 和
[微信输入法](https://z.weixin.qq.com/)，或其他兼容的“按住说话”输入法。

1. **安装固件和 App。** 用 USB 数据线将设备连接电脑，打开
   [FoloToy 玩法页](https://ai-passport.folotoy.cn/plays/2174/)，按页面指引直接安装固件。
   再从 [GitHub Releases](https://github.com/lialun/folotoy-vibe-companion/releases/latest)
   下载配套版本的 Mac App，拖入“应用程序”。详细配置和按键说明见
   [设备使用指南](docs/vibe/README.zh_CN.md)。
2. **用手机给设备联网。** 连接设备配网热点，选择 2.4 GHz Wi-Fi。
   最多记住五个网络，在家和公司等已配置网络间自动重连。
3. **配对设备。** 在 App 输入设备显示的配对码。需要语音输入时，再在输入法中选择
   BlackHole 2ch，设置相同的“按住说话”快捷键，为 App 授予快捷键控制权限。
   只用 Agent 提醒无需配置语音。首次打开和授权流程见
   [Mac 安装指南](docs/vibe/install.zh_CN.md)。
4. **接入你使用的 Agent。** 在“Agent 提醒”安装集成，按 Agent 要求审核信任，
   再用一次真实事件确认提醒。

设备通过局域网发送麦克风音频，语音识别由输入法完成，遵循该输入法的设置及服务规则。
设备不是通用 USB 或蓝牙麦克风。语音和 Agent 提醒要求客户端运行、电脑保持唤醒；
番茄钟和天气不依赖客户端持续运行。

### Mac 首次打开与授权

将 App 拖入“应用程序”后打开。若提示无法验证开发者，进入
**系统设置 → 隐私与安全性 → 仍要打开**。

若提示“已损坏”且没有“仍要打开”，确认安装包来自[本仓库 Release](https://github.com/lialun/folotoy-vibe-companion/releases/latest)，
核对附件 `SHA256SUMS.txt` 后，在终端执行：

```bash
xattr -dr com.apple.quarantine "/Applications/Vibe 伴侣.app"
```

重新打开 App。系统询问时允许局域网访问；使用语音与回车还需在
**系统设置 → 隐私与安全性 → 辅助功能**中开启 **Vibe 伴侣**。
更新后若已勾选却仍提示未授权，删除旧条目，再添加“应用程序”中的新版 App。

## 平台与兼容性

- **已实现：** ESP32-C3 固件和 macOS 客户端，Windows／Linux 客户端尚未实现。
- **验证范围：** 开发期间已在 Apple Silicon 和目标设备上测试；Intel 版本已交叉编译，
  Intel 实机和干净系统安装仍待验证。收音机由电脑转发，需客户端运行且电脑联网；支持 Wi-Fi 与 BLE，播放效果取决于电台源和链路。

## 开发与贡献

固件使用 **ESP-IDF 5.5.3**，客户端使用 **Swift、SwiftUI 和 AppKit**，不打包 Electron。

```bash
# 仓库检查与主机测试，不需要设备
./tools/validate.sh --static

# 完整验证与合并固件构建；先激活 ESP-IDF 5.5.3
./tools/validate.sh

# macOS 通用 App、原生测试、DMG 和 ZIP；在 Mac 上运行
bash macos/build.sh
```

[构建与打包](docs/vibe/release.zh_CN.md) ·
[交互、UI 与内存规范](docs/vibe/DESIGN.zh_CN.md) ·
[参与贡献](.github/CONTRIBUTING.zh_CN.md) · [反馈问题](.github/SUPPORT.zh_CN.md)

## 致谢与许可

基于官方 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 固件及 BSP，参考
[passport-os](https://github.com/manchunx7-bit/passport-os) 和
[folo-ai-passport-voice](https://github.com/manchunx7-bit/folo-ai-passport-voice)。
Agent 接入参考 [agent-notify](https://github.com/hellolib/agent-notify)、
[agent-notify-dsh](https://github.com/wanfanggreat/agent-notify-dsh) 和
[PeonPing](https://github.com/PeonPing/peon-ping)。天气来自
[Open-Meteo](https://open-meteo.com/)，电台搜索使用
[Radio Browser](https://www.radio-browser.info/)。

这是社区项目，并非 FoloToy 官方发行版。项目代码采用 [MIT 许可](LICENSE)，
字体、图标和固件依赖保留各自许可，发行包附带第三方声明。
详见[素材与许可记录](assets/README.zh_CN.md)。
