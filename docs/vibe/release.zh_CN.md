**简体中文** · [English](release.md)

# 构建与分发

本项目**只通过 GitHub Releases 分发**，采用免费的 **ad-hoc 临时签名**。
不需要购买 Apple 开发者会员，也不需要上架 App Store。安装包未经 Apple 公证，
首次打开时用户可能需要为 App 单独确认；[安装说明](install.zh_CN.md)包含操作步骤和更新后的权限处理。
这种签名方式也可公开分发。具体版本的发布状态取决于对应验证记录，包括干净系统
安装和 Intel 实机检查。

## 本地固件

安装并激活 ESP-IDF **5.5.3**，目标 ESP32-C3，工具链不要加入提交。
运行 `./tools/validate.sh`，包含官方与本应用的主机测试、独立目录构建、合并镜像校验和配套调试归档。
交付文件是 `build/FoloToy-AI-Passport-full.bin`，从 `0x0` 刷入。
使用 `tools/archive_firmware.py verify` 校验 `build/firmware/<sha256>/` 中的归档。
保持 8 MB factory 分区布局，合并刷写可能重置 NVS；如需保留设置，先阅读
[固件布局](../development/engineering/firmware-layout.zh_CN.md)再选择兼容的分段升级。

## 本地 Mac App

开发机需要 Swift 和 macOS SDK，通常来自 Xcode Command Line Tools 或完整 Xcode。
只有开发者需要编译器，最终用户不需要。

```bash
bash macos/build.sh
```

脚本构建 `arm64` 和 `x86_64`，合并成通用应用，签名，运行原生协议测试，
再在 `build/macos/` 生成 DMG 和 ZIP。仅开发自用时可以设置 `VIBE_ARCHS=arm64`。
App 直接使用 Apple 系统框架，最终用户无需安装 Python、Homebrew 或 JavaScript 运行环境。
语音输入另需安装 BlackHole 2ch 和微信输入法等兼容输入法；只用 Agent 提醒无需安装这些组件，
也无需快捷键控制权限。组件安装和辅助功能／局域网权限不能由 App 静默授予。

DMG 内包含 App、指向“应用程序”的快捷方式和中英文安装说明。
ZIP 是包含同一个 App 的替代下载。App 资源中包含许可及第三方声明。
`SHA256SUMS.txt` 使用可移植的文件名，默认不使用 Apple 证书或账号秘密。

构建脚本仍保留未来可选的 Developer ID 签名和公证能力，但它不属于这次选定的免费分发流程。
可选方案需要自己的 [Developer ID](https://developer.apple.com/developer-id/)、钥匙串证书和 `notarytool` 配置：

```bash
VIBE_VERSION=0.1.0 \
VIBE_SIGN_IDENTITY='Developer ID Application: Your Name (TEAMID)' \
VIBE_NOTARIZE=1 VIBE_NOTARY_PROFILE=vibe-notary \
bash macos/build.sh
```

脚本开启 Hardened Runtime，将 ZIP 提交公证，附加并验证票据，使用 Gatekeeper 检查，最后重建 ZIP。
如果 Command Line Tools 缺少 `notarytool`／`stapler`，使用完整 Xcode。
无需 App Store 审核。证书、私钥、Apple 凭证不要放进仓库或 Release 附件。

## GitHub Actions

现有工作流执行仓库／主机检查，并构建设备固件和通用 Mac App。
手动运行只产生可下载的 Actions 构建产物，不发布 Release。
主动推送 `v*-vibe-companion` 格式标签后，生成包含两端安装包的**草稿预发布**。
只有发布任务有仓库写权限。自动构建的 Mac 包使用临时签名，不使用 Apple 账号秘密。
`tools/package_release.py` 生成双语安装附件、合并的[发布说明](release-notes.zh_CN.md)，
以及所有上传文件的校验值。

附件包含合并固件、通用 DMG、通用 ZIP、`INSTALL.txt`、`INSTALL.zh_CN.txt`、
`THIRD_PARTY_NOTICES.txt` 和 `SHA256SUMS.txt`。
本地生成相同发布目录时，将固件、DMG、ZIP 和许可声明复制到一个空目录，
执行 `python3 tools/package_release.py <目录>`。

正式发布前：完成设备验收，在干净的 Apple Silicon 和 Intel Mac 上测试，
准备中英文 changelog，选择版本和 `v0.1.0-vibe-companion` 一类标签，
检查并更新双语草稿说明，保留免费安装包，不引入付费签名依赖；附上字体／源码许可和 SHA-256 校验值。
本地命令行测试不代表 GitHub Actions 已实际运行成功。

创建仓库、推送源码、推送发布标签和发布草稿，是分别对外生效的操作。
应先完成具体文件、检查后再执行已获授权的动作。本轮开发不会自动发布任何内容。

## 本地发布审阅

每轮候选包使用新的输出目录，避免混入旧版本：

```bash
VIBE_MAC_OUTPUT="$PWD/build/review/macos" bash macos/build.sh
```

真正准备发布时，通过 `VIBE_VERSION` 选择最终语义版本；上述示例不代表已选定公开版本。
打标签前审阅中英文发布概览，补充候选版本的实际验证结果。安装附件和发布说明应在
离开源码目录后仍然可读。发布打包主机测试检查完整／缺失／多版本冲突、可重复且使用
相对文件名的校验值，以及保留许可。

PR／分支检查新增独立的 `macos-checks.yml`，覆盖原生 Swift 测试和通用安装包。
上游基线同步仅手动运行，发布本应用时不自动把上游合并到应用工作中。

Mac 上解析好托管组件后，运行 `bash tools/render_readme.sh` 重新生成 README 图片。
工具以示例数据渲染生产 UI，不替换已安装 App，不与设备通信。
