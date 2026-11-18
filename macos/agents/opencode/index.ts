import { Plugin } from "@opencode/plugin"
import { createAdapter } from "./adapter.mjs"
import { createQuotaCollector } from "./quota.mjs"
export default Plugin.define({
  id: "vibe-companion-opencode",
  setup(ctx) {
    const controller = new AbortController()
    const quota = createQuotaCollector({ integration: ctx.integration })
    const quotaTimer = setInterval(() => { void quota.tick() }, 30000)
    quotaTimer.unref?.()
    const adapter = createAdapter({ getSession: (sessionID) => ctx.session.get({ sessionID }), directory: ctx.location.directory })
    void (async () => {
      try {
        for await (const event of ctx.event.subscribe({ types: ["session.execution.started", "session.execution.succeeded", "session.execution.failed", "permission.asked", "form.created"], signal: controller.signal })) {
          quota.activity()
          await adapter.event(event)
        }
      } catch { /* Unload or bridge failure must never affect a session. */ }
    })()
    return () => { controller.abort();clearInterval(quotaTimer);quota.close() }
  },
})
