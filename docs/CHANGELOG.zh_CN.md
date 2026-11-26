<p align="right">
  <strong>简体中文</strong> · <a href="CHANGELOG.md">English</a>
</p>

# Changelog

> **由发布负责人维护：** 普通功能、应用和文档 PR 不修改本文件。创建发布 tag 前，
> 发布负责人检查已合并的用户可见变化，并与 `CHANGELOG.md` 同步更新。当前
> `Unreleased` 下的条目是下次发布的待核对内容，不是已经定稿的发布历史。

## Unreleased

## v1.0.1 - 2026-11-26

- 大幅优化多种网络条件下的语音稳定性，强化 Wi-Fi／蓝牙双链路互补与单链路短时恢复，减少短暂抖动、发送拥堵引起的中断和漏音。
- 使用独立链路重试与电脑接收确认，已收到的音频不再占用另一条链路的重复发送机会；增加有界语音缓存并改进补发、排序与连续播放。
- 语音与收音机共用音频工作区，录音优先使用缓存，语音收尾后恢复电台，降低两项功能同时争用内存的压力。
- 明确录音开始、结束、取消及输出排空的归属；回车必须来自独立按键，保留去重与取消隔离，并修复时钟偏差导致正常回车持续被拒绝的问题。
- 已知限制：极弱单 Wi-Fi 仍可能音频超时；弱 Wi-Fi 叠加主动丢包的压力测试出现过一次 20 ms 播放空档。收音机经蓝牙传输时仍可能卡顿；不承诺所有网络下零丢包。

## v1.0.0 - 2026-11-24

- Wi-Fi 与蓝牙均已授权连接时，同时传输语音，电脑端合并并去重；一条链路中断后，可由另一条继续承载录音，无需重新开始输入。
- 新增有限音频历史与缺块补发，修复单链路短暂丢包；Wi-Fi／蓝牙遇到临时发送压力时进行有限重试，不积压数秒前的旧语音。
- 稳定链路选择，避免短暂抖动引发切换；保留语音尾字，等待语音输出完成后执行回车。全断链或休眠时安全停止，不自动续录。
- 仅录音期间申请语音调度资源，使用后释放补发状态和临时缓存；旧端保持原语音协议，新的恢复能力需要配套升级桌面端和固件。
- 已知问题：收音机改走蓝牙且重传积压时仍可能卡顿，本版未包含电台调度修复。

## v0.9.1 - 2026-11-23

- 电脑休眠时停止蓝牙扫描与连接、局域网重试、音频和额度查询，休眠通知不等待网络或音频清理。
- 系统唤醒后恢复通信，不依赖屏幕是否亮起；保留主动断开状态，避免旧回调重新连接或误断开新会话。
- 休眠前释放输入按键、在关闭链路前取消电台列表保存；因休眠中断的额度查询可在唤醒后重试，成功查询间隔与失败退避保持不变。

## v0.9.0 - 2026-11-18

- 新增 Codex、Claude Code、OpenCode Go 剩余额度卡片，复用 Agent 已有登录；近期使用时自动采集，数据不可用或过期时安静隐藏。
- 额度页排在四页天气之前，统一计算页码；支持三个独立比例周期同屏展示，包括 Go 的五小时、每周、每月额度。
- 桌面端新增“接入自己的 Agent”，提供同时覆盖本机任务提醒和额度回调的提示词；支持自定义名称、比例周期与独立金额余额。
- 改善蓝牙数据收发，避免无关订阅状态变化关闭应用通知通道。

## v0.8.4 - 2026-11-17

- 蓝牙和 Wi-Fi 均支持语音、回车、提醒及收音机；公司网络不便让设备接入时，可直接通过蓝牙连接电脑。
- 合并两条链路发现的同一设备，电脑发起、设备确认，替代配对码；更换电脑也在设备上确认。
- 收音机改由电脑解码，通过任一链路播放；改进缓冲、重连、语音抢占及自动恢复。请配套升级固件和 Mac App。
- 全新设备可从蓝牙或可选 Wi-Fi 设置开始；优化热点发现响应，增加十秒未弹页时的浏览器地址提示。
- 黑屏时首次菜单或连接确认操作只亮屏，首页语音与回车仍可直接使用。
- 简化输入配置、BlackHole 直接下载、输入法设置及 Agent 安装指引。

本地 Intel／设备验收覆盖语音、回车、电台与恢复、BLE 重新配对、手机 Wi-Fi、已安装提醒入口及从零连接。耗电／电池、干净系统、最低 macOS／Apple Silicon 实机和真正多电脑／多设备尚未完成对应实测。安装包采用 ad-hoc 临时签名，未经 Apple 公证。

## v0.8.3-vibe-companion - 2026-11-10

Vibe 伴侣首个公开版本，包含 FoloToy AI Passport 配套固件和原生 macOS 客户端。

- 无线按住说话输入及独立回车，番茄钟或收音机运行时仍可使用语音。
- 接入 Codex、Claude Code、OpenCode、DeepSeek Harness、ZCode、Gemini CLI 和 Qwen Code；紧凑浮窗亮屏响铃，同一会话短时间内重复事件只提醒一次。
- 手动承接下一阶段的专注／休息轮转、多页天气、亮暗主题与定时切换。
- 桌面端搜索电台、检测自定义音频流、调整列表和顺序；语音时让出音频通道，结束后恢复连接。
- 记住多个 Wi-Fi 并自动重连、自动选择空闲通信端口、设备与客户端配置同步。
- 菜单栏常驻客户端、按需读取的待机／内存诊断、中英文安装说明，以及 Apple Silicon／Intel 通用 DMG 和 ZIP。

Intel 实机、干净系统安装仍待验证。安装包使用 ad-hoc 临时签名，未经 Apple 公证。

## 上游继承记录

以下未发布记录来自官方固件基线，保留供参考，不作为 Vibe 伴侣的发布历史。

### 上游待发布改动

- 加入厂家为优特利 520mAh 电芯生成的 80 字节 CW2017 profile，并实现内容与更新标志检查、写入后校验、规定的重启时序以及有上限的 SOC 就绪等待。

- 扩充环境引导文档：新增乐鑫 Git 服务镜像（`git.espressif.com.cn`）作为中国大陆首选线路，覆盖 ESP-IDF v5.5.3 及其子模块；补充子模块长等待/超时处理、原地修复，以及 `esp32-wifi-lib` 等大仓的按钉死 commit 浅取；提示按仓库残留的 Jihulab `insteadOf` 旧配置；并把官方离线 release 压缩包加入兜底方案（经验来自 `esp-mosaico/esp-mosaico-vibe`）。

- 按功能域整理文档并采用双入口：根目录 `AGENTS.md` 变为薄路由（只保留硬约束与任务路由），详细的 AI 开发工作流下沉到 `docs/development/ai-guide.md`，`agent-guide.md` 并入其中。为 `docs/development/` 增加二级分区（`engineering/`、`ci/`、`release/`），把 `plays/` 应用档案与 `experiences/` 移入带专属 README 的 `docs/reference/` 参考区；删除 `docs/software-design/`（空脚手架）；把 `assets/{fonts,images,music}/README` 三个叶子 README 并入 `assets/` README；把 `project-completion` 的六个子文档压平为单文件；并把每个目录统一为单一 README，消除所有 `INDEX` 文件与一处重复经验索引。所有交叉引用与文献链接已更新；未丢弃任何内容。

- 删除位于 `0x700000` 的旧 app/test 分区，以及相关的 bootloader、校验和
  文档要求；固定的 `cardid` 保护分区及其 CI 校验保持不变。
- 规定多应用发布的 Release 标题约定：tag 按 `v<版本>-<应用名>`（如 `v0.1.0-voice-keychain`）命名，让 Release 标题同时带版本与应用名；发布成功后核对标题，保证一眼扫 Release 列表就能区分是哪个应用。
- 新增发布后收尾流程：`issue-suggestions` skill 用于把用户反馈作为 issue 提交到上游项目；`experience-pr` skill 用于把可复用的开发经验作为文档 PR 提交；新增 `docs/experiences/` 目录保存单条经验文件；并配套 `project-completion`、`file-issues` 与经验索引文档。
- 精简仓库根目录：将 GitHub 可识别的社区治理文档迁入 `.github/`，将变更记录迁入 `docs/`，同步全部引用，并在仓库检查中加入根目录文档白名单。
- 全仓库文档语言规范：所有维护中的 Markdown 默认 `.md` 文件使用英文，简体中文使用配对的 `.zh_CN.md`，双方提供语言切换；静态检查会阻止缺失配对、缺失切换链接或英文默认页混入中文正文。
- AI 开发流程一期：精简按任务加载的上下文入口，统一本地/CI 验证脚本，新增 PR 自动构建与模板，并提交依赖锁文件以提高构建可复现性。
- PR 审查修复：GitHub Actions 固定到完整 commit SHA，构建与发布 job 按最小权限拆分，同步 checkout 关闭凭证持久化；补充 Feature Request / Usage Question issue 表单；启用并修正私密安全报告兜底说明；清理 README 路径、CI 触发条件与历史分支描述漂移。
- 语言规范变更：commit 标题、PR 标题与 body 由"默认中文"改为**使用英文**（`docs/contribution/commit-and-pr.md` 更新）；中文写作规范（全角标点）适用范围剔除 PR/MR 描述（`doc-conventions.md` 更新）。
- CI 构建改造：`build-firmware.yml` 显式传入 `SDKCONFIG_DEFAULTS=sdkconfig.defaults` 再 `idf.py build`，由 defaults 启用自定义分区表（`CONFIG_PARTITION_TABLE_CUSTOM=y`，文件名为 `partitions.csv`）；`CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE` 改为 `n`，再用 `idf.py merge-bin -o build/FoloToy-AI-Passport-full.bin` 合并可直刷完整固件；产物精简为仅 full.bin；`actions/cache` 升级到 v5 以消除 GitHub Actions Node.js 20 弃用警告；CI 文档同步更新。
- 合并上游 PR #6（wireless-low-power-demos）以解决 PR #4 冲突：引入无线/低功耗 demo（`main/demo_wifi.c`、`demo_ble.c`、`demo_radio.c`、`demo_low_power.c`）、`partitions.csv`（NVS/PHY/3 MB factory-app 分区）、`main/CMakeLists.txt`/`main.c`/`demo.h`/`sdkconfig.defaults` 更新；同步硬件指南的 Wi-Fi/BLE/低功耗章节；README 能力契约表补充 Wi-Fi/Bluetooth LE/Low power 三项（中英双语）。
- 提交规范补充：`docs/contribution/commit-and-pr.md` 明确 PR 标题与 commit 标题使用相同的 Conventional Commit 格式和英文祈使句，不用名词短语当标题。
- CI 与文档清理：`sync-main.yml` 移除 `test_mode` 残留模板注释；`docs/development/coding-conventions.md` 将「Redis TTL」条目泛化为「缓存组件」条目（当前固件无 TTL 约束需求，消除从模板带入的无关约定）。
- 补充通用规范（借鉴 Shinku）：`docs/contribution/doc-conventions.md` 新增中文全角标点规范（正文 `，`；`（`）`，代码/命令/路径保留英文原样）、凭证不入仓规范（token/密钥/私钥绝不入仓，提交前 git diff 扫描敏感前缀）、文件删除安全规范（删除走系统回收站，不用 rm -rf/git clean -fd）。
- 代码注释规范强化：`docs/development/coding-conventions.md` 补充完善注释要求——函数说明（用途/参数/返回值/副作用/线程上下文/内存所有权/初始化顺序）、变量说明（语义/取值范围/生命周期/同步要求）、逻辑注释（状态机/时序/寄存器/魔数依据），覆盖范围宁多勿少，中文注释保留英文技术术语。
- 文档去 AI 化：`docs/README.md` / `docs/README.zh_CN.md` 移除 AI 专属章节（Entry point、Source-of-truth、提需求格式、BSP 边界、Runtime invariants、验收交付格式、构建命令），README 只保留给人看的项目介绍、硬件能力契约、demo 案例与项目结构；构建命令章节删除（与 `docs/development/build-and-test.md` 重复）。
- 新增 `docs/development/agent-guide.md`：集中承载"AI 如何在本仓库工作"（上下文建立顺序、事实来源优先级、提需求格式、BSP 边界、运行时规则、交付格式），并链接 build-and-test 与硬件指南，不重复构建命令与验收矩阵。
- 同步更新索引：`AGENTS.md` 规则索引新增 agent-guide 条目；`docs/INDEX.md` 与 `docs/development/README.md` 新增 agent-guide 索引行。
- 文档补充：`docs/fork-guide.md` 说明「为什么根目录不放置 README」——根目录 README 预留给 fork 开发者自行放置（上游留空），fork 后可将自己的内容写入根目录 `README.md` 介绍 fork 后的项目；GitHub 显示优先级（根 README > docs/README.md）契合该预留意图。
- 分支合并：创建 `main-update` 分支（基于与上游一致的 main），将 `feature/repo-structure`、`ci/build-firmware`、`ci/sync-main` 三个分支合并进来，统一 docs 结构（CI 文档归入 `docs/development/`，workflow 文件随 ci 分支引入 `.github/workflows/`）；解决 development/software-design README 的 add/add 冲突。
- 合并后审查修复：`docs/INDEX.md` 补充 CI 文档索引；`docs/fork-guide.md` 修正 workflow 引用为 `.github/workflows/sync-main.yml`；`docs/README` 双语项目结构块补充 `.github/workflows/` 与 CI 文档说明。
- ci 分支 CI 文档路径调整：`ci/build-firmware` 的 `docs/software-design/CI-build-and-release.md` 与 `ci/sync-main` 的 `docs/software-design/CI-sync-main.md` 均移入各分支的 `docs/development/`（CI 属工程规范）；`docs/software-design/README.md` 保留为软件设计索引；feature 分支的 software-design 索引同步更新引用。
- fork 补充文档目录迁移：`assets/docs/` 移至 `docs/assets/`（文档素材归入 docs/ 更合理），新增 `docs/assets/.gitkeep` 空目录占位；同步更新 AGENTS.md / INDEX / doc-conventions / fork-guide 的路径引用。
- 文档结构调整：根目录不再放 README——上游英文 README 移入 `docs/README.md`、中文移入 `docs/README.zh_CN.md`（GitHub 从 docs/ 识别主 README）；原 `docs/README.md` 根总索引更名为 `docs/INDEX.md`；同步更新 AGENTS.md / CONTRIBUTING / SUPPORT / fork-guide / doc-conventions 的路径引用。
- 初始化项目文档：新增 `AGENTS.md`、`CLAUDE.md` 和 `CHANGELOG.md`。
- 仓库结构规整：上游英文 `README.md` 更名为 `README.en_US.md`，保留 `README.zh_CN.md`。
- 新增目录骨架：`docs/`（software-design / hardware-design）、`assets/`（fonts / images / music，各含 `README.md`）、`skills/`。
- 将上游硬件开发指南归位到 `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`。
- 文档规范：子目录 readme 统一为大写 `README.md`；补充 fork 用户约定（main 只动根 README）。
- 扩展 fork 用户约定：`main` 分支允许修改根目录 `README.md` 和 `assets/docs/`（README 不足以说明项目时存放补充文档与素材）。
- 新增 `assets/docs/` 目录约定：上游 main 只保留空目录 `.gitkeep`，内容文件仅存在于 fork；使用方法规范写入 AGENTS.md「给 fork 用户」约定。
- CI 文档迁移：`docs/software-design/CI.md` 从本分支移除，迁至 `ci/build-firmware` 分支并改名为 `docs/software-design/CI-build-and-release.md`。
- 补充 `main` 分支策略说明：解释 `main` 保持干净的两大原因（与上游同步无冲突 + 多小项目按分支整理）；例外——执意 main 开发需停用 CI 自动同步；提醒 fork 用户默认 action 关闭需手动启用（此条为整个 CI 的通用要求，统一写入 AGENTS.md）。
- 文档拆分：将 `AGENTS.md` 按主题拆为公共文档——新增 `docs/contribution/`（doc-conventions.md、commit-and-pr.md）与 `docs/development/`（build-and-test.md、coding-conventions.md），新增 `docs/fork-guide.md`；`AGENTS.md` 精简为简介 + 项目概述 + 必读文档索引。
- 同步更新索引：`docs/software-design/README.md`、`README.en_US.md` / `README.zh_CN.md` 的 `docs/` 目录说明。
- 参考 cindy 仓库文档组织完善索引：新增 `docs/README.md` 根总索引；AGENTS.md 规则索引按触发场景改写（附触发条件）；`docs/contribution/` 与 `docs/development/` 的 README 补充收录标准。
- 引入社区治理文档（参照 cindy 改写，放仓库根目录）：新增 `CONTRIBUTING.md` / `.zh_CN.md`（贡献指南，针对 ESP-IDF/AI agent/fork 场景改写）、`CODE_OF_CONDUCT.md` / `.zh_CN.md`（贡献者公约）、`SECURITY.md` / `.zh_CN.md`（安全报告流程）、`SUPPORT.md` / `.zh_CN.md`（支持渠道）；AGENTS.md 与 docs/README.md 同步引用。
