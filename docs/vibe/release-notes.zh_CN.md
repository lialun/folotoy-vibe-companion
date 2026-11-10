**简体中文** · [English](release-notes.md)

# Vibe 伴侣 · 发布概览

面向 FoloToy AI Passport 的社区固件及 macOS 客户端，具体版本以 Release 标签和安装包
文件名为准。

## 包含功能

- 使用设备麦克风进行无线“按住说话”输入，单独按键负责回车；番茄钟或收音机运行时
  仍可使用语音。
- 接入 Codex、Claude Code、OpenCode、DeepSeek Harness、ZCode、Gemini CLI 和 Qwen Code，
  事件覆盖按具体集成区分。首条提醒立即发送，同一 Agent／会话随后一秒内的其他回调
  被忽略，不转发对话内容。
- 紧凑提醒浮窗，亮屏响铃、五秒自动消失，按键关闭时仍执行原功能。
- 手动开始下一阶段的专注／休息轮转、四页天气、亮暗主题与定时切换，以及最多 20 台
  电台的桌面搜索、选台和排序。
- 手机配置最多五个已记住 Wi-Fi，自动重连；客户端自动使用空闲端口发现设备，支持
  设备与桌面配置同步。
- 菜单栏 macOS App，一个 DMG／ZIP 同时包含 Apple Silicon 和 Intel；配套 ESP32-C3、
  8 MB Flash 的已验证合并固件。

## 安装

使用同一 Release 的固件和 App。附件 `INSTALL.txt`／`INSTALL.zh_CN.txt` 说明 Mac 安装、
BlackHole 2ch、输入法、授权和配对。源码中的 `docs/vibe/README.zh_CN.md` 说明设备刷写和
操作。合并固件 `FoloToy-AI-Passport-full.bin` 从 `0x0` 写入，全新安装可能重置设备设置。
下载后对照 `SHA256SUMS.txt`。

安装包使用免费 ad-hoc 临时签名，未经 Apple 公证。首次打开或更新可能需要单独确认及
刷新快捷键权限。不需要付费 Apple 账号，App 不使用钥匙串。

## 验证边界

构建、主机测试和设备验收分别报告。Intel 实机、干净系统安装仍待验证。当前客户端
仅支持 macOS，一台设备配对一台电脑。Agent 覆盖取决于版本和本地入口；收音机取决于
电台源、网络、格式及可用设备资源。配套包附第三方声明，正式发布前还需核对该版本的
最终验证结果。
