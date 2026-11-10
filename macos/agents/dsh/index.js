// Lifecycle forwarding adapted from agent-notify-dsh (MIT); see NOTICE.txt.
import { spawn } from "node:child_process"
const helper = "__VIBE_HELPER__"
export const name = "vibe-companion-dsh"
export const inject = []
export function register(ctx, forward) {
  function root(agent) {
    // Cordis optional service access must use get(), not ctx.agents?.roots().
    try {
      const roots = ctx.get?.("agents")?.roots?.()
      return Array.isArray(roots) ? roots.includes(agent) : true
    } catch { return true }
  }
  function notify(kind, payload) {
    try {
      const agent = payload?.agent
      if (!agent || !root(agent)) return
      const header = agent.session?.header ?? agent.header ?? {}
      forward(kind, { session_id: header.sessionId ?? header.sessionID ?? header.id ?? agent.session?.id ?? "unknown", turn_id: payload.turnId ?? payload.turnID ?? "", request_id: payload.id ?? "", cwd: header.cwd ?? header.directory ?? agent.cwd ?? "" })
    } catch {}
  }
  ctx.on("approval/request", (request, next) => {
    notify("approval", request)
    return next() // Preserve the exact continuation result and rejection.
  })
  ctx.on("user-questions/request", (request, next) => {
    notify("input", request)
    return next()
  })
  ctx.on("agent/turn-stopping", payload => { notify("completed", payload) })
  ctx.on("agent/error", payload => { notify("failed", payload) })
}
export function apply(ctx) {
  register(ctx, (event, payload) => {
    try {
      const child = spawn(helper, ["--vibe-agent-hook", "dsh", event], {detached:true, stdio:["pipe","ignore","ignore"]})
      child.on("error", () => {});child.stdin.on("error", () => {})
      child.stdin.end(JSON.stringify(payload));child.unref()
    } catch {}
  })
}
