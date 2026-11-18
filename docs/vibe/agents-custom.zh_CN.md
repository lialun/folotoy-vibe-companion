<p align="right"><strong>简体中文</strong> · <a href="agents-custom.md">English</a></p>

# 接入自己的 Agent

在桌面端打开 **Agent 提醒 → 接入自己的 Agent**，复制提示词交给你的编程 Agent。提示词同时包含提醒和额度回调。打开弹窗时会准备当前用户专用的 helper：`~/Library/Application Support/io.github.vibecompanion.mac/agent-bridge`。

用参数数组启动 helper，通过标准输入发送一个 UTF-8 JSON 对象，随后关闭标准输入。helper 一秒内结束并输出 `{}`，不代替用户批准操作，也不改变 Agent 的决定。桌面端未运行或设备离线时安静跳过，不需要额外服务器或推送账号。

## 任务提醒

参数：`--notify`。

```json
{"schema":1,"source":"my-agent","name":"My Agent","event":"completed","session_id":"session-123","event_id":"turn-456"}
```

事件为 `completed`、`approval`、`input`、`failed`。映射真实的主任务事件，过滤子任务、暂时 idle 和正常取消。同一事件重试时保留 `event_id`，不同事件使用不同标识。设备显示传入的 Agent 名称。

## 额度

参数：`--quota`。时间戳需替换为实际采集、重置时间，单位为 Unix 秒。

```json
{"schema":1,"source":"my-agent","name":"My Agent","account":"opaque-account","sample_id":"observation-123","observed_at":1790000000,"items":[{"label":"Weekly","remaining":73.5,"window_minutes":10080,"resets_at":1790100000}]}
```

使用 Agent 已有登录态提供的真实额度，不用本机 Token 计数或估算的上限。`remaining` 为剩余百分比，范围 0–100，零是合法值。周期分钟数、重置时间可省略。独立金额余额可将 `remaining` 替换为 `"amount":"12.50","currency":"USD"`。每次包含一至四项。

重发同一次观察时，保留 `sample_id` 和 `observed_at`。样本 930 秒后过期，每项也会在其重置时间到达后隐藏；重发不会续期。桌面端与设备最多保留七个有效额度来源。

近期使用时每五分钟采集一次，空闲十分钟停止。失败退避十／二十分钟，服务端要求更长等待时遵循服务端。取不到时安静跳过。helper 和设备不会自行获取凭据或查询自定义供应商。

## 字段限制与隐私

- `source`：稳定的小写 ASCII 标识，匹配 `[a-z0-9][a-z0-9._-]{0,63}`；自定义来源不能冒用内置来源身份。
- `name`：1–32 字节可打印 ASCII；每项 `label`：1–24 字节可打印 ASCII，兼容设备固定字库。
- 会话、事件、账号、样本标识：最多 200 字节 UTF-8。来源、账号、会话标识在传至设备前进行摘要处理。
- JSON 输入：最多 4096 字节。只转发展示元数据，不传凭据、提示词、回复、工具参数和文件内容。

回调不阻塞任务，保留已有 Hook，并提供仅移除适配器自身配置的卸载方法。复制的提示词包含这些要求。
