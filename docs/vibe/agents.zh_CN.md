[English](agents.md) · **简体中文**

# Agent 通知

Vibe 伴侣接收本机 Agent 的生命周期事件，向配对设备发送简短提醒。
设备亮屏、显示 Agent 和事件，并播放现有提示音。语音优先，无需云端推送账号。

## 集成范围

| Agent | 接入方式 | 配置的事件 |
| --- | --- | --- |
| Codex | 用户命令 Hook | Stop、PermissionRequest，以及实际提供回调的仅匹配提问工具的 PreToolUse |
| Claude Code | 用户命令 Hook | Stop、PermissionRequest、提问工具 PreToolUse、MCP 交互请求 Notification |
| OpenCode 2 | 原生 V2 服务端插件 | 执行完成／失败、审批请求、表单创建 |
| DeepSeek Harness | 原生 Cordis 插件包 | 本轮停止、审批请求、用户问题、错误 |
| ZCode | hooks.enabled + hooks.events | Stop、PermissionRequest |
| Gemini CLI | 用户命令 Hook | AfterAgent、Notification(ToolPermission) |
| Qwen Code | 用户命令 Hook | Stop、PermissionRequest、支持的提问工具 PreToolUse |

这是适配器能力，不代表所有版本、所有入口已经通过端到端实测。客户端区分检测到、
已安装和实际收到事件。Gemini、ZCode 暂未配置通用的结构化提问回调。Codex 的工具
Hook 可能存在覆盖例外。Stop 只报告自身停止边界，其他插件仍可能让 Agent 继续工作。
首条提醒立即转发；同一 Agent、同一已知会话，首条之后一秒内的其他类型提醒也忽略，
忽略事件不延长这个窗口。不同 Agent／会话互不影响，缺少会话标识时不做会话合并。
保留原有三秒精确事件重试去重，电脑端记录有界。不保存延迟候选，不依赖取消回调。
普通工具成功／失败、泛化的空闲、启动和已处理事件均不产生提醒。

## 会话名称

只有现有提醒事件直接携带的可读对话名才符合展示条件。没有名称时，继续展示 Agent
名称和提醒状态。Session ID、UUID、项目目录、表单／通知标题、提问或回答的片段都不
作为对话名。不为此增加元数据查询、会话文件／数据库读取、生命周期监听、标题缓存或
自动生成名称。

以下为 2026-11-10 核对当前接入契约的结果：

| Agent | 现有提醒事件是否提供对话名？ | 核对的契约 |
| --- | --- | --- |
| Codex | 未文档化此字段 | [Hook 输入](https://learn.chatgpt.com/docs/hooks#common-input-fields)提供会话标识，没有对话标题。 |
| Claude Code | 已接入的提醒事件未文档化此字段 | [`session_title`](https://code.claude.com/docs/en/hooks#sessionstart-input) 文档列于 SessionStart 和 UserPromptSubmit，本项目未订阅这两个事件。 |
| OpenCode 2 | V2 2.2.5 提醒事件结构无此字段 | 完成／审批事件携带会话标识；表单创建事件的标题属于表单。[会话 API](https://opencode.ai/docs/build/plugins/server/)需要读取会话元数据。 |
| DeepSeek Harness | 已订阅的请求／回合／错误事件及 Session header 没有对话标题 | 独立的[会话标题服务](https://deepseek-harness.github.io/deepseek-harness/en/reference/subsystems/session-title)需要补查标题。 |
| ZCode | 未文档化此字段 | [Hook 输入契约](https://zcode.z.ai/en/docs/hooks)列出会话标识和事件字段。 |
| Gemini CLI | 未文档化此字段 | [公共及提醒事件输入](https://geminicli.com/docs/hooks/reference/)提供会话标识和通知／回合内容，没有会话标题。 |
| Qwen Code | 当前提醒事件输入类型无此字段 | [Hook 类型](https://github.com/QwenLM/qwen-code/blob/754792b8d199c603360fe6e712544b7fe10be953/packages/core/src/hooks/types.ts)区分会话标识和通知标题。 |

因此，针对这些契约暂不实现会话名展示。设备协议、固定提醒队列和浮窗保持现有布局与
内存占用。未来某个 Agent 版本支持时，先重新核对实际提醒事件；产品其他位置能查到
标题，不代表符合这项约定。

## 安装

打开伴侣的「Agent 提醒」，重新检测，为需要的 Agent 安装集成。安装器增量合并自己
的条目，保留其他配置，建立私有备份；移除时仅删除自己的条目。JSON/JSONC 会序列化
为 JSON；注释、排版保留在备份中，重写后的文件不保留。格式损坏或不支持时拒绝重写。
支持客户端进程继承的自定义配置目录环境变量。Finder 启动的 App 可能没有继承终端的
变量；使用自定义目录时，需让伴侣在相同环境中启动。

仅安装能够产生人工关注提醒的回调：Codex 3 个、Claude Code 4 个、Qwen Code 3 个、
Gemini CLI 2 个、ZCode 2 个。Claude 的第四个回调仅匹配实际 MCP 表单／URL 请求，
不处理空闲和重复的延迟审批通知。OpenCode 只订阅四种提醒事件；DSH 保留四个提醒
监听，均移除开始／已回复／已取消等辅助事件。更新会移除自己的旧回调，保留其他
Hook 和已有文件描述；Codex 各项带有用途状态说明。伴侣启动时更新已准备的原生
插件文件；对应 Agent／服务端需重新加载，才能运行新代码。

Codex 必须在自身完成 Hook 信任审查：终端输入 `/hooks`，桌面版进入「设置 → Hooks」。
伴侣不修改信任记录。其他 Agent 安装后重新打开或重新加载会话。Codex 本地桌面和
终端使用用户级 Hook；远程／云端会话不在本地 helper 的覆盖范围。
OpenCode 插件以 V2、2.2.5 为基线，不适用于旧 V1 接口；同一服务端下的终端和桌面
会话均可使用该服务端插件。

DSH 的「准备插件」会生成本地插件包；检测到 `dsh` 命令时，通过 DSH 自身的插件命令
安装到 `web` 配置。桌面配置归 DSH App 管理，需在它的插件管理器中添加生成的目录。
准备后点击「复制插件路径」，在 DSH 中打开「插件」→「添加插件」，粘贴路径后点击
「安装」。也可以点击「显示插件」在 Finder 中定位。
仅准备文件不显示为安装成功，不绕过 DSH 去修改包管理锁文件或桌面配置。

「测试提醒」只检查伴侣到设备的链路，实际收到 Agent 事件才检查到适配器。
屏幕和声音仍需观察。伴侣必须保持运行，电脑保持唤醒，设备可以连接。

## 传输、隐私和耗电

通用架构的 App 可执行文件也提供命令行 Hook helper，无需 Python、额外守护进程或
固定端口。App 监听系统分配的本机 UDP 端口，把随机令牌和端口写入当前账户私有文件。
helper 每次重新读取，因此重启、换端口不需要重新安装集成。helper 的输入大小与等待
时间均有限，返回空 JSON 对象和成功退出码，不批准、不拒绝、不回答 Agent 的请求。
DSH 的原生 waterfall 监听器即便转发出错，也原样返回后续处理的结果。

桌面端标准事件只保留 Agent、事件类型、会话标识、简短项目目录名和时间，不转发或
轮询提示词、回答、工具参数、会话文件和凭据。发送到设备的只有固定 Agent／事件编码、
随机事件 ID、过期时间，通过原有配对局域网连接传输。本机 IPC 与设备配对使用不同
令牌。同一账户的其他程序可以读取本地端点，这不是同一账户程序之间的隔离边界。

桌面端最多发送四次，五秒未确认则过期；离线事件直接丢弃，不在重新连接后补发。
设备使用 272 字节固定队列／去重结构：四条待处理事件、十六个近期 ID、一条当前事件。
不新增设备任务、套接字、PCM 缓冲、定时器或保活。待处理事件十秒后过期；提醒最多
显示五秒，有新事件时可在两秒后替换。紧凑浮窗下的原页面继续运行，番茄倒计时不停止。
第一次实体按键按下时关闭当前提醒及已经排队的提醒，然后继续执行这次按键的原有功能，
不额外增加点击或确认。保留近期 ID，重试包不会让提醒重新出现，新事件仍可正常到达。
提醒不写 Flash。

浮窗仅占 240 × 320 屏幕的 x=8..211、y=118..197，保留语音区、状态栏和右侧按键栏。
小型状态徽标搭配 16 px Agent 名称、20 px 事件文字，分别适配暖亮色与暗色主题。
在原页面之后直接绘制，不创建子控件、截图、背景遮罩或新的字体／图片资源。
原有 26 KiB LVGL 池及收音机／语音缓冲均不变。绘制状态增加四个静态字节，回调记录与
临时绘制使用原有 LVGL 池。目标链接结果中 BSS 段计入对齐后增加八字节，LVGL 池
仍为 26 KiB。在同一页面、同样整帧重绘条件下，主机测得浮窗最多增加 376 字节绘制峰值
（测得整帧峰值最高 19,832 字节）。这是 64 位主机结果，不等于设备堆测量。
两种主题、十种背景、2,000 次显示／关闭后，
LVGL 留存分配没有增长，浮窗外像素完全一致，关闭后原页面完整恢复。

录音期间延后新提醒。原有音频任务使用常驻语音 PCM 播放提示音，收到语音按键时让出。
收音机若正在工作，先释放播放资源，提示音后通过同一任务重新连接。现有收音机的内存
和网络限制仍然存在，本功能不声称修复它们。实际待机电流与最差情况堆／栈增量仍需
硬件测量；272 字节只描述状态结构本身。

## 参考与验证

适配器综合参考 [agent-notify](https://github.com/hellolib/agent-notify)、
[agent-notify-dsh](https://github.com/wanfanggreat/agent-notify-dsh)、
[PeonPing](https://github.com/PeonPing/peon-ping)。MIT 许可保留于 `assets/licenses/`
并进入分发包。接口核对了各 Agent 官方文档和 OpenCode 2.2.5 插件包。

主机测试覆盖七种事件转换、选择性与幂等配置合并／移除、换端点后的独立 helper 进程
投递、DSH 后续处理结果与异常保持、OpenCode 主会话过滤及忽略 idle、固定队列过期／
去重，以及现有音频生命周期回归。真实会话验收仍按入口分别进行：在每个已安装 Agent
触发完成、审批、支持的提问，确认设备亮屏、提示一次、不重复，以及语音输入及时。
