<p align="right"><a href="agents-custom.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Connect your own Agent

In the desktop app, open **Agent alerts → Connect your own Agent**, then copy the prompt to your coding Agent. It describes both notification and quota callbacks. Opening the sheet prepares the account-private helper at `~/Library/Application Support/io.github.vibecompanion.mac/agent-bridge`.

Launch the helper with an argument array, send one UTF-8 JSON object through stdin and close stdin. It finishes within one second, prints `{}`, and never grants approval or changes the Agent’s decision. Missing/offline companions are skipped. No additional server or push account is needed.

## Task alerts

Argument: `--notify`.

```json
{"schema":1,"source":"my-agent","name":"My Agent","event":"completed","session_id":"session-123","event_id":"turn-456"}
```

Events are `completed`, `approval`, `input`, and `failed`. Map actual root-task events; exclude child tasks, temporary idle, and normal cancellation. Keep the same `event_id` across retries and use a new one for each occurrence. The device displays the supplied Agent name.

## Quotas

Argument: `--quota`. Replace the timestamps with the actual observation and reset times, in Unix seconds.

```json
{"schema":1,"source":"my-agent","name":"My Agent","account":"opaque-account","sample_id":"observation-123","observed_at":1790000000,"items":[{"label":"Weekly","remaining":73.5,"window_minutes":10080,"resets_at":1790100000}]}
```

Use actual allowance data from the Agent’s existing login, not local token counts or inferred limits. `remaining` is percent left, from 0 to 100; zero is valid. Window minutes and reset timestamps are optional. For independent monetary balances, replace `remaining` with `"amount":"12.50","currency":"USD"`. Each sample contains one to four items.

Keep `sample_id` and `observed_at` unchanged when retrying the same observation. Samples expire after 930 seconds, and each row disappears when its reset time passes. A replay never extends the lifetime. The desktop/device retain at most seven live quota sources.

Collect every five minutes during recent use; stop after ten idle minutes. Back off ten/twenty minutes on failure, or longer when the service requires it. Skip missing data quietly. The helper and device do not fetch credentials or query custom providers themselves.

## Field limits and privacy

- `source`: stable lowercase ASCII identifier matching `[a-z0-9][a-z0-9._-]{0,63}`; custom sources cannot impersonate built-in identities.
- `name`: 1–32 printable ASCII bytes; item `label`: 1–24 printable ASCII bytes, compatible with the device’s fixed font.
- Session, event, account and sample identifiers: at most 200 UTF-8 bytes. Source/account/session identifiers are hashed before device transport.
- JSON input: at most 4096 bytes. Only display metadata is forwarded; credentials, prompts, replies, tool inputs and file contents stay outside the callback.

Keep callbacks non-blocking, preserve existing hooks, and provide an uninstall that removes only the adapter’s entries. The copied prompt includes these requirements.
