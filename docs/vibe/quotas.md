<p align="right"><a href="quotas.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Agent quotas

Quota collection uses the existing Agent integration controls. There is no quota login, API-key form, browser-cookie import, or Keychain prompt. Collection failures are local diagnostics only; they never create an error card or interrupt Agent notifications.

## Implemented sources

- Codex: a bounded, bridge-owned `codex app-server` child reads the existing ChatGPT account and `account/rateLimits/read`. The account is checked before and after the read. No conversation or inference request is created. A weekly-only response stays weekly-only. API-key accounts are not treated as subscription quotas.
- Claude Code: the integration wraps the official status-line command. It forwards the `rate_limits` fields during actual use and preserves the original command's output. Repeated redraws retain their observation ID and capture time, including across desktop restarts. Only normalized counters are forwarded, never the original status-line payload. Existing installations can use **Update integration**; removal restores the previous status-line object when the wrapper is still ours.
- OpenCode Go: the installed plugin resolves the Agent’s currently selected OpenCode connection and reads the official Go usage endpoint. Five-hour, weekly and monthly allowances retain independent percentages and reset times. No credentials leave that Agent process or are sent to the bridge.
- Custom Agents: **Connect your own Agent** prepares the helper and provides a copyable prompt covering notifications and quotas. [Local callback contract](agents-custom.md).
- Other notification integrations remain available. Additional quota sources are not enabled without a verified one-click path and real remaining-quota data. Local token counts are not a substitute.

## Activity and cadence

Codex integration callbacks supply activity. Startup/recovery also checks only modification times of at most 64 session files for each of today and yesterday under the selected `CODEX_HOME`. It does not read conversation contents. A recent file modification is an activity hint, not proof that an inference request is still running. The fallback stops after ten minutes without activity. Explicit completion/end callbacks suppress older activity hints.

The first active observation can trigger a read. Queries are coalesced per source, run off the UI/network queues, and normally wait five minutes between attempts. Failures back off to ten and twenty minutes. Successful quota reads do not themselves count as Agent activity. Claude receives new observations instead of polling an authenticated service. Frequent observations are coalesced to the latest sample for five-minute delivery, retaining the original capture time.

## Freshness and ownership

The display lifetime is 930 seconds: three normal query intervals plus thirty seconds of scheduling allowance. Only a new successful observation starts a lifetime. Network retries and reconnects keep the original observation ID and remaining lifetime. The device deduplicates retries and rejects older observations. Each quota window also stops being displayed when its reported reset time passes; no local reset to full allowance is inferred.

Quotas are ephemeral. Switching authorization or unpairing clears them; a temporary transport interruption does not extend their lifetime. No quota is saved to device NVS. Credentials remain on the computer. The protocol carries only an opaque ownership fingerprint and display fields.

## Presentation contract

Firmware capability `agentQuota: 1` accepts authenticated `quota.cache` messages with schema 1. Up to seven source cards each contain at most four metrics. Names/labels use bounded printable ASCII; fixed captions and standard window names use the verified Chinese font. Unknown metric types and malformed values are rejected silently. Each message fits the existing 1536-byte control limit.

The common templates display a single percentage, two independent percentage windows, per-model pages, or independent currency balances. Three percentage windows share a compact page; other sets use two metrics per page, with extra metrics on another page. Currency balances are never added or turned into a percentage. Very large amounts use an explicitly approximate compact notation on the device; desktop values retain the full recorded amount. Positive fractions do not appear as zero.

Valid quota pages come first, followed by the four weather pages when weather is enabled. Both use the same global page index and total; one quota page plus weather reads 1/5 through 5/5. Weather and valid quota pages rotate every eight seconds on the idle, lit Home screen. SETTINGS click advances manually and restarts the dwell time. Voice, timers, radio, connection prompts, and Agent notices take priority. Rotation does not reset the screen timeout or wake a dark screen. Expired or absent quotas contribute no page.

## Validation scope

Host tests cover weekly-only/zero/missing data, malformed protocol fields, retry lifetime, reset expiry, dynamic pagination, account-private status-line backup/restore, repeated redraws, and carousel pauses. The pinned LVGL renderer checks both themes with the real fonts and layout bounds. Hardware and live-account acceptance must be reported separately; synthetic Claude fixtures are not a real Claude account test.
