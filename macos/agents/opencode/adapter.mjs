import { spawn } from "node:child_process"
const helper = "__VIBE_HELPER__"
export function send(event, payload) {
  try {
    const child = spawn(helper, ["--vibe-agent-hook", "opencode", event], { detached: true, stdio: ["pipe", "ignore", "ignore"] })
    child.on("error", () => {})
    child.stdin.on("error", () => {})
    child.stdin.end(JSON.stringify(payload)); child.unref()
  } catch {}
}
export function createAdapter({getSession, directory = "", forward = send}) {
  return { async event(event) {
    try {
      const kind = ({"session.execution.succeeded":"completed", "session.execution.failed":"failed", "permission.asked":"approval", "form.created":"input"})[event.type]
      if (!kind) return
      const data = event.data ?? event.properties ?? {}
      const sessionID = data.sessionID ?? data.form?.sessionID ?? data.session?.id ?? data.request?.sessionID ?? event.durable?.sessionID
      if (!sessionID) return
      const session = await getSession(sessionID)
      if (!session || session.parentID || session.parentSessionID || session.parent?.id) return
      // No status(idle) shortcut: idle may mean a live question/permission dialog.
      forward(kind, {session_id: sessionID, turn_id: data.executionID ?? event.id ?? "", request_id: data.requestID ?? data.form?.id ?? data.id ?? "", cwd: session.directory ?? directory})
    } catch {}
  }}
}
