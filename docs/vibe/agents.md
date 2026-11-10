**English** · [简体中文](agents.zh_CN.md)

# Agent notifications

Vibe Companion receives local Agent lifecycle events and forwards short alerts
to the paired toy. The toy wakes its screen, displays the Agent and event, and
plays its existing chime. Voice retains priority. No cloud push account is needed.

## Integration coverage

| Agent | Adapter | Events configured |
| --- | --- | --- |
| Codex | User command hooks | Stop, PermissionRequest, question-only PreToolUse where emitted |
| Claude Code | User command hooks | Stop, PermissionRequest, question-only PreToolUse, MCP elicitation Notification |
| OpenCode 2 | Native V2 server plugin | execution succeeded/failed, permission asked, form created |
| DeepSeek Harness | Native Cordis bundle | turn stopping, approval request, user questions, error |
| ZCode | hooks.enabled + hooks.events | Stop, PermissionRequest |
| Gemini CLI | User command hooks | AfterAgent, Notification(ToolPermission) |
| Qwen Code | User command hooks | Stop, PermissionRequest, supported question-only PreToolUse |

These are adapter capabilities, not a claim that every release or entry point
has passed an end-to-end test. The app distinguishes detection, installation,
and receipt of a real event. Gemini and ZCode have no general structured-question
callback configured. Codex tool hooks can have coverage exceptions. A Stop hook
reports its own stop boundary, not proof that another plugin will not continue
work. The first alert is forwarded immediately. For one Agent and known session,
additional alerts within one second of that first alert are suppressed across
event kinds. The window does not extend when a callback is suppressed. Other
Agents/sessions remain independent; missing session identities are not grouped.
Existing exact-event retry filtering remains three seconds, with bounded desktop
bookkeeping. There is no delayed candidate queue or cancellation hook.
Ordinary tool successes/failures, generic idle, startup and resolved events do
not generate alerts.

## Session names

Only a readable conversation name carried directly by the existing alert event
qualifies for display. Missing names leave the Agent and action unchanged.
Session IDs, UUIDs, project directories, form/notification titles and excerpts
from prompts or replies are not conversation names. Do not add metadata queries,
transcript/database reads, lifecycle listeners, title caches or generated names
to fill this gap.

The current integration contracts were checked on 2026-11-10:

| Agent | Existing alert event provides a conversation name? | Contract checked |
| --- | --- | --- |
| Codex | No documented field | [Hook inputs](https://learn.chatgpt.com/docs/hooks#common-input-fields) provide identity, not a conversation title. |
| Claude Code | No documented field in the configured alert events | [`session_title`](https://code.claude.com/docs/en/hooks#sessionstart-input) is documented for SessionStart and UserPromptSubmit, which this integration does not subscribe to. |
| OpenCode 2 | No field in the V2 2.2.5 alert schemas | Execution/permission events carry session identity; a created form's title names the form. The [session API](https://opencode.ai/docs/build/plugins/server/) would require reading session metadata. |
| DeepSeek Harness | No conversation title in the subscribed request/turn/error payloads or Session header | The separate [session-title service](https://deepseek-harness.github.io/deepseek-harness/en/reference/subsystems/session-title) requires a title lookup. |
| ZCode | No documented field | The [hook input contract](https://zcode.z.ai/en/docs/hooks) documents session identity and event-specific fields. |
| Gemini CLI | No documented field | The [base and alert inputs](https://geminicli.com/docs/hooks/reference/) expose identity and notification/agent content, not a session title. |
| Qwen Code | No field in the current alert input types | The [hook types](https://github.com/QwenLM/qwen-code/blob/754792b8d199c603360fe6e712544b7fe10be953/packages/core/src/hooks/types.ts) distinguish session identity from notification titles. |

Accordingly, session-name display is not implemented for these contracts. The
device protocol, fixed alert queue and overlay retain their existing layout and
memory footprint. Recheck the actual alert contract before enabling a name for
any future Agent version; a title available elsewhere in that product does not
meet this policy.

## Installation

Open **Agent notifications** in the companion, refresh detection, then install
an integration for each desired Agent. Installers merge owned entries, preserve
unrelated settings, make private backups, and remove only their own entries.
JSON/JSONC is serialized to JSON; comments and formatting are retained in the
backup, not in the rewritten file. Malformed/unsupported shapes fail without
rewriting that file. Custom configuration homes are honored when inherited by
the app. A desktop app launched by Finder may not inherit terminal environment
overrides; use the matching configuration home when launching the companion.

Only callbacks that can produce an attention-seeking alert are installed:
Codex 3, Claude Code 4, Qwen Code 3, Gemini CLI 2 and ZCode 2. Claude's fourth
callback is limited to actual MCP form/URL requests; idle and duplicate delayed
permission notifications are excluded. OpenCode subscribes to four alert
events; DSH registers four alert listeners. Both omit start/replied/cancelled
bookkeeping. Config updates remove owned legacy callbacks while preserving
unrelated hooks and existing file descriptions. Codex callbacks have identifiable
status messages. Prepared native plugins are refreshed on companion startup;
reload the corresponding Agent/server to activate their new code.

Codex requires its own hook trust review: CLI `/hooks`, or desktop Settings >
Hooks. The companion never edits hook trust records. Restart/reload other Agent
sessions after installation. Local Codex desktop and CLI share the user hook
configuration; remote/cloud sessions are not covered by the local helper.
The OpenCode plugin targets V2 (2.2.5 baseline), not the older V1 plugin API.
It is a server plugin usable by terminal and desktop sessions on that server.

For DSH, **Prepare plugin** stages a local bundle. With a detected `dsh` CLI it
installs into the `web` profile using DSH's own plugin command. The desktop
profile is owned by DSH's app; add the staged directory in its plugin manager.
After preparation, click **Copy plugin path**, open **Plugins > Add plugin** in
DSH, paste the path and click **Install**. Use **Show plugin** to locate it in Finder.
Staging alone is not shown as a successful installation. It does not hand-edit
DSH package-manager lockfiles or a desktop profile behind the application's back.

A test alert checks companion-to-device delivery only. Receipt of a real event
checks the Agent adapter. Screen rendering and sound still require observation.
The companion must be running, the computer awake, and the toy reachable.

## Transport, privacy and power

The universal app executable also supplies a bounded command-line hook helper;
users need no Python, extra daemon or fixed port. The app listens on an
OS-assigned loopback UDP port and writes a random token plus port to an
account-private endpoint file. Every helper invocation rereads it, so port
changes and restarts do not require reinstalling integrations. The helper has
bounded input size/time and returns an empty JSON object with exit status zero.
It never approves, denies or answers an Agent request. Native DSH waterfall
listeners return the exact continuation result even if forwarding fails.

Normalized desktop events include Agent, event kind, session identity, a short
project basename and timestamp. Prompts, responses, tool arguments, transcript
files and credentials are not forwarded or polled. Only the fixed Agent/event
codes, random event ID and expiry travel to the toy using its existing
paired LAN connection. Desktop IPC and device pairing use separate tokens.
Same-account programs can read the local endpoint; it is not an isolation
boundary against other programs running as that account.

Desktop sends at most four LAN attempts and expires an unacknowledged alert
after five seconds. Offline alerts are dropped instead of replayed later.
Device state is a 272-byte fixed queue/deduplication structure: four pending
items, sixteen recent IDs and one active item. There is no new task, socket,
PCM buffer, timer or keepalive on the toy. Queued events expire after ten
seconds. A compact overlay is visible for up to five seconds; a fresh queued
event can replace it after two seconds. The original page continues underneath,
including Pomodoro deadlines. The first physical press closes the notice and
its already-queued backlog, then continues the original gesture without an
extra click or confirmation. Recent IDs remain remembered to suppress retries.
New events can still arrive normally. Notices do not write Flash.

The overlay occupies only x=8..211, y=118..197 on the 240 x 320 display, leaving
the voice area, status bar and key rail intact. A small semantic badge precedes
the 16 px Agent name and 20 px action, with distinct warm-light/dark palettes.
It draws after the original page without child widgets, a screenshot, a backdrop
or a new font/image asset. The existing 26 KiB LVGL pool and radio/voice buffers
remain unchanged. Packed draw state adds four static bytes; callback bookkeeping
and transient drawing use the existing LVGL pool. The target link map shows an
eight-byte increase in the BSS section including alignment; the pool remains
26 KiB. On the same live page with equal full-frame invalidation, host rendering
measured at most 376 extra peak bytes for drawing the overlay (largest tested
full-frame peak: 19,832 bytes). This 64-bit host measurement is not a target heap
measurement. Across both themes, ten backgrounds and 2,000
show/hide cycles, retained LVGL allocation did not grow and all pixels outside
the overlay stayed identical. Dismissal restored the original rendered page.

Recording defers a new alert. The existing audio owner uses the resident voice
PCM block for the chime, yielding when a voice press arrives. Radio, if active,
is released for the chime and subsequently reconnects through the same owner.
Existing radio memory/network limitations remain; this feature does not claim
to fix them. Actual standby-current and worst-case heap/stack deltas need board
measurements; the 272-byte figure describes the state structure only.

## References and verification

The adapter design combines [agent-notify](https://github.com/hellolib/agent-notify),
[agent-notify-dsh](https://github.com/wanfanggreat/agent-notify-dsh), and
[PeonPing](https://github.com/PeonPing/peon-ping). Their MIT notices are included
in `assets/licenses/` and distributable packages. API contracts were checked
against official Agent documentation and the OpenCode 2.2.5 plugin package.

Host checks cover seven Agent mappings, selective/idempotent configuration
merge/removal, actual helper-process delivery after endpoint replacement,
DSH continuation identity/error propagation, OpenCode root filtering and idle
suppression, fixed-queue expiry/deduplication, and existing audio lifecycle
regressions. Complete real-session acceptance remains entry-point specific:
trigger completion, approval and a supported question in each installed Agent,
then verify wake, one chime, no duplicate and timely voice input on the toy.
