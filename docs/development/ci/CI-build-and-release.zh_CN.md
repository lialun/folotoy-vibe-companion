<p align="right">
  <strong>简体中文</strong> · <a href="CI-build-and-release.md">English</a>
</p>

# 自动构建与发布（CI / Build & Release）

本仓库提供 GitHub Actions 自动构建流水线，构建设备固件和通用 Vibe 伴侣 Mac App。
发布标签的所有检查通过后会公开 Release；手动运行分支只生成 Actions 构建产物。推送标签前应完成文件审阅并取得发布授权。

本文件与 `.github/workflows/build-firmware.yml` 一同维护，工作流行为变化时必须同步更新。

## 触发条件

- **push tag**：推送 `vMAJOR.MINOR.PATCH` 格式标签（如 `v0.8.4`）时触发，所有检查和构建成功后公开包含两端安装包的 Release。
- **workflow_dispatch**：可在 GitHub Actions 页面手动触发（用于调试/预发布验证）。

> 普通分支 push 不触发这个工作流，可从 Actions 手动运行。

## 流水线做了什么

1. **ccache 缓存恢复**：使用 `actions/cache` 缓存编译中间产物（`.ccache`），二次编译大幅提速。缓存 key 含 ref 与 commit SHA；缓存保留时间以仓库的 GitHub Actions 设置为准。
2. **编译与验证**（ESP-IDF 5.5.3 / esp32c3）：运行与本地相同的 `./tools/validate.sh --firmware`。脚本使用 `sdkconfig.defaults` 和 `partitions.csv` 构建固件，再执行 `idf.py merge-bin`。
3. **验证完整固件**：脚本按 `flash_args` 生成的实际偏移逐字节确认 bootloader、partition-table 和 app，确认使用 8 MB Flash，并检查当前配置的分区布局，最后输出 `build/FoloToy-AI-Passport-full.bin`。仓库默认偏移为 `0x0`、`0x8000` 和 `0x10000`；用户自定义分区表时可以改变 app 偏移。
4. **上传 artifact**：每次成功构建都上传 `FoloToy-AI-Passport-full.bin`。普通分支只有从该分支手动运行 `workflow_dispatch` 才会构建；普通 push 不触发。
5. **主机检查**：独立 checks job 运行 `./tools/validate.sh --static`，检查仓库、工作流与主机逻辑。
6. **Mac App**：运行 `bash macos/build.sh`，执行 C／Swift 协议测试，构建 arm64／x86_64 通用 App，临时签名并生成 DMG 和 ZIP。
7. **标签发布**：前三类检查／构建全部通过后，独立 release job 下载产物，生成双语安装／发布说明及所有文件的校验值，公开 Release。默认使用免费 ad-hoc 签名，不使用付费 Apple 账号或 Apple 密钥，也未经 Apple 公证。

构建 job 只有 `contents: read` 权限；仅 release job 在 tag 发布时获得 `contents: write`。所有 Action 均固定到完整 commit SHA，行尾注释保留对应发布版本，升级时需同时核对 SHA 与版本。

## 产物

- `FoloToy-AI-Passport-full.bin`：合并固件，从 `0x0` 写入。
- 通用 Mac DMG 和 ZIP，支持 Apple Silicon／Intel。
- 双语安装说明、第三方许可和 SHA-256 校验值。

完整附件清单与免费分发步骤见 [Vibe 发布准备](../../vibe/release.zh_CN.md)。

## 在线烧录

使用浏览器在本机完成写入与校验，固件不会上传服务器。打开 **在线刷机工具**：

`https://ai-passport.folotoy.cn/tools/web-flasher/`

步骤：连接设备（USB JTAG/serial debug unit）→ 选择本 Release 的合并固件 `FoloToy-AI-Passport-full.bin` → 选择波特率（如 460800）→ 开始写入。目标是 8MB Flash 板卡，无需其它参数。

## 发布版本

本仓库只发布一个产品，标签与 Release 标题统一使用 **`vMAJOR.MINOR.PATCH`**，
例如 `v0.8.4`，不加应用名后缀。对应 Mac 安装包与 App 版本为 `0.8.4`。
工作流监听 `v*` 标签，由 `tools/release_version.py` 校验完整的三段数字格式；
格式不正确或带后缀的标签无法发布。分支上手动运行只上传构建产物。

已有标签和已发布的 Release 保留原名，不为调整命名而改名或重新发布。

## 变更日志准备

普通功能、应用和文档 PR 不修改中英文变更日志。创建并推送发布 tag 前，发布负责人需要：

1. 确认上一版本，并检查此后已合并的 PR，以及对应的产品和应用权威文档。
2. 汇总用户可见功能、修复、兼容性变化和发布流程变化；忽略内部重构、CI 维护、拼写修复与生成文件刷新。
3. 同时更新 `docs/CHANGELOG.md` 和 `docs/CHANGELOG.zh_CN.md`，把本次发布条目放入
   `## <tag> - YYYY-MM-DD` 标题下，保留一个新的 `Unreleased` 章节，再提交发布准备变更。

现有 `Unreleased` 条目只作为待核对输入：必须与实际合并内容及目标版本核对，不能直接照搬。该发布准备
变更是共享变更日志的唯一写入方，并且必须包含在 tag 所指向的 commit 中。

## Release 说明

tag 触发的 Release 只有在合并固件与它的 Release 说明一起发布时才完整。发布 Release 后，要写一份
说明，向可能没读过仓库的用户解释这次构建。覆盖三块：

- **功能（What's new / 功能）**：本次 Release 相对上一版新增或变更的功能、行为或修复。面向用户，
  不是 commit 日志。
- **方法（How to build / 方法）**：运行 `./tools/validate.sh --firmware` 生成并校验合并固件，
  明确从 `0x0` 烧录的产物为 `FoloToy-AI-Passport-full.bin`。单独执行 `idf.py build`
  仅做增量编译，不生成也不校验合并完整镜像，不能作为交付命令的替代。
- **使用（How to use / 使用）**：如何烧录（上方在线刷机工具），以及本次 Release 的关键交互或硬件
  要求。

用英文写 Release 说明（项目双语时再配一份简体中文），并在 GitHub/GitLab Release 上链接它们。
用户可见摘要以发布准备阶段的变更日志为依据，确保两者一致。

## 相关文件

- `.github/workflows/build-firmware.yml`：本流水线定义。
- 详见 `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`（硬件/烧录细节）。

## 桌面端 PR 检查

`.github/workflows/macos-checks.yml` 独立验证涉及桌面端的 PR、匹配的 `main` 推送和手动触发。
它运行通用构建、原生协议／Agent 测试、签名检查和 DMG 校验，仓库权限只读，不创建发布。
