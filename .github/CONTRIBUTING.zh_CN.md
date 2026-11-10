简体中文 | [English](/.github/CONTRIBUTING.md)

# 参与 Vibe 伴侣开发

欢迎改进设备固件、桌面客户端、Agent 适配、设计和文档。先阅读[项目介绍](/README.md)
和[设计规范](/docs/vibe/DESIGN.zh_CN.md)。涉及较大交互或架构变化时，先在本仓库 Issues
讨论问题与预期，再开始实现。

## 开发入口

- 设备应用：`main/vibe/`，复用 `components/bsp/` 的硬件驱动。
- macOS 客户端和 Hook 适配：`macos/app/`、`macos/agents/`。
- 产品文档：`docs/vibe/`。根 README 介绍项目；详细操作、默认值和实现记录放入指南。
- 固件工具链：ESP-IDF 5.5.3。桌面端：Swift 与 macOS SDK，目标为 macOS 13+、arm64
  和 x86_64，见[构建说明](/docs/vibe/release.zh_CN.md)。

从包含 Vibe 应用的分支新建工作分支。上游 FoloToy 硬件测试基线不是本应用。保留已有修改，
不提交生成的构建结果和本机配置。AI 辅助开发遵循 [AGENTS.md](/AGENTS.md)。

## 修改与验证

修改应便于独立审查，说明具体问题、最终行为、兼容性或存储影响以及验证结果。
测试覆盖有意义的行为和失败情况，不把编译通过当作设备验收。

```bash
./tools/validate.sh --static  # 仓库检查和主机测试
./tools/validate.sh          # 完整固件验证；先激活 ESP-IDF
bash macos/build.sh          # Mac 上构建通用 App、原生测试和安装包
```

固件／UI 修改应保持三键交互、语音优先、低功耗和内存所有权约定。说明新增常驻内存、
峰值分配及未验证的设备行为；按原生分辨率检查两种主题和中文字形。不要为了简化页面
改动音频／网络资源所有权。刷机需得到设备所有者授权。

Agent 适配应保留无关用户配置，只安装本项目拥有的 Hook，确保 helper 执行有界、失败时
不阻塞 Agent。不向设备发送提问、回答、工具参数或会话文件；检查重复回调、不同会话，
以及声明支持的实际 Agent 入口。

## 提交 Pull Request

标题应说明具体改动，例如 `fix(radio): retain buffered input across decode`。
UI 变更附相关截图，分开报告构建、主机测试、设备测试。同步更新权威文档的中英文版本。
普通修改不更新上游公共变更日志；发布准备负责整理版本发布说明。除非确实要向上游贡献，
PR 应指向本项目的应用分支，不是 FoloToy 官方仓库。

贡献代码遵循 [MIT 许可](/LICENSE)，保留第三方许可。遵守[行为准则](/.github/CODE_OF_CONDUCT.zh_CN.md)，
安全问题通过[私密报告流程](/.github/SECURITY.zh_CN.md)提交。
不要包含凭证、配对令牌、个人数据或未脱敏日志。
