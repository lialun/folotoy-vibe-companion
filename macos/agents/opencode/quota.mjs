import { spawn } from 'node:child_process'
import { createHash, randomUUID } from 'node:crypto'
const helper = "__VIBE_HELPER__"
export function forwardQuota(payload) {
  try {
    const child = spawn(helper, ['--vibe-quota-hook', 'opencode'], {detached:true, stdio:['pipe','ignore','ignore']})
    child.on('error',()=>{});child.stdin.on('error',()=>{})
    child.stdin.end(JSON.stringify(payload));child.unref()
  } catch {}
}
export function normalizeGo(body, account, now=Date.now()) {
  if (!body?.usage) return
  const items=[]
  for (const [key,label,minutes] of [['rolling','5h',300],['weekly','Weekly',10080],['monthly','Monthly',43200]]) {
    const row=body.usage[key]
    if (!row || !['ok','rate-limited'].includes(row.status) || typeof row.percent!=='number' || !Number.isFinite(row.percent) || row.percent<0 || row.percent>100) return
    const reset=typeof row.resetsAt==='string' ? Date.parse(row.resetsAt):NaN
    if (!Number.isFinite(reset) || reset<=now || reset-now>366*86400000) return
    items.push({label, remaining:row.status==='rate-limited'?0:100-row.percent, window_minutes:minutes, resets_at:reset/1000})
  }
  return {schema:1,source:'opencode-go',name:'OpenCode Go',account:createHash('sha256').update(account).digest('hex'),sample_id:randomUUID(),observed_at:now/1000,items}
}
// The plugin uses its Agent's selected connection; no separate login or credential file scan.
export function createQuotaCollector({integration,fetcher=fetch,forward=forwardQuota,clock=()=>Date.now(),report=()=>{}}) {
  let activity=-Infinity,next=0,failures=0,busy=false,closed=false,controller
  async function poll() {
    const now=clock()
    if (closed || busy || now<next || now-activity>=600000) return
    busy=true;next=now+300000;controller=new AbortController()
    const signal=controller.signal
    const timeout=setTimeout(()=>controller?.abort(),8000);timeout.unref?.()
    try {
      const execute=async()=>{
        if (signal.aborted)return
        const connection=await integration.connection.active('opencode')
        if (!connection || signal.aborted) return
        const credential=await integration.connection.resolve(connection)
        if (signal.aborted || !credential || !['key','oauth'].includes(credential.type)) return
        const org=credential.metadata?.orgID,token=credential.type==='oauth'?credential.access:credential.key
        if (typeof org!=='string' || !org || typeof token!=='string' || !token) return
        const configured=credential.metadata?.server ?? 'https://opencode.ai/console'
        const server=new URL(configured)
        // Never forward an official credential to an arbitrary host or follow redirects.
        if (server.protocol!=='https:' || !['opencode.ai','console.opencode.ai'].includes(server.hostname) || server.port || server.username || server.password) return
        const response=await fetcher('https://opencode.ai/zen/go/v1/usage',{headers:{authorization:`Bearer ${token}`,'x-opencode-org-id':org,accept:'application/json'},signal,redirect:'error'})
        if (!response.ok) {
          const seconds=Number(response.headers?.get('retry-after'))
          const date=Date.parse(response.headers?.get('retry-after') ?? '')
          const wait=Number.isFinite(seconds) && seconds>0 ? seconds*1000:Number.isFinite(date)?date-clock():0
          if (wait>0)next=Math.max(next,clock()+Math.min(wait,86400000))
          throw Error('unavailable')
        }
        const text=await response.text()
        if (text.length>32768)throw Error('oversize')
        const payload=normalizeGo(JSON.parse(text),org,clock())
        if (!payload)throw Error('invalid')
        if (!closed && !signal.aborted)forward(payload)
      }
      await Promise.race([execute(),new Promise((_,reject)=>signal.addEventListener('abort',()=>reject(Error('timeout')),{once:true}))])
      failures=0
    } catch {
      failures=Math.min(3,failures+1);next=Math.max(next,clock()+Math.min(1200000,300000*2**failures))
      report('OpenCode Go quota unavailable; retry deferred')
    } finally {clearTimeout(timeout);busy=false}
  }
  return {activity(){activity=clock();void poll()},tick:poll,close(){closed=true;controller?.abort()}}
}
