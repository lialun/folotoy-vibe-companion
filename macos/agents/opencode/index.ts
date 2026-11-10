import { Plugin } from "@opencode/plugin"
import { createAdapter } from "./adapter.mjs"
export default Plugin.define({
  id: "vibe-companion-opencode",
  setup(ctx) {
    const controller = new AbortController()
    const adapter = createAdapter({ getSession: (sessionID) => ctx.session.get({ sessionID }), directory: ctx.location.directory })
    void (async () => {
      try {
        for await (const event of ctx.event.subscribe({ types: ["session.execution.succeeded", "session.execution.failed", "permission.asked", "form.created"], signal: controller.signal })) {
          await adapter.event(event)
        }
      } catch { /* Unload or bridge failure must never affect a session. */ }
    })()
    return () => controller.abort()
  },
})
