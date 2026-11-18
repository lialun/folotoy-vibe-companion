import assert from 'node:assert/strict'
import {register} from '../../macos/agents/dsh/index.js'
import {createAdapter} from '../../macos/agents/opencode/adapter.mjs'
const events=new Map(), notices=[],root={session:{header:{id:'root',cwd:'/project'}}},child={session:{header:{id:'child'}}}
register({on:(name,callback)=>events.set(name,callback),get:()=>({roots:()=>[root]})},(kind,payload)=>notices.push({kind,payload}))
assert.equal(events.has('agent/created'),false)
let invoked=0
const answer=Promise.resolve({answer:'unchanged'})
assert.equal(events.get('approval/request')({agent:root,id:'r'},()=>{invoked++;return answer}),answer)
assert.equal(invoked,1);assert.equal(notices.at(-1).kind,'approval');assert.equal(notices.at(-1).payload.session_id,'root')
assert.equal(events.get('user-questions/request')({agent:root},()=>answer),answer)
const failure=new Error('original')
assert.throws(()=>events.get('approval/request')({agent:root},()=>{throw failure}),error=>error===failure)
events.get('agent/turn-stopping')({agent:child});assert.equal(notices.filter(x=>x.kind==='completed').length,0)
events.get('agent/turn-stopping')({agent:root});assert.equal(notices.at(-1).kind,'completed')
const broken=new Map()
register({on:(key,callback)=>broken.set(key,callback),get:()=>{throw Error('service unavailable')}},()=>{throw Error('bridge unavailable')})
assert.equal(broken.get('approval/request')({agent:root},()=>answer),answer)
const sent=[]
const adapter=createAdapter({getSession:async id=>({id,parentID:id==='child'?'root':undefined,directory:'/tmp/project'}),forward:(kind,payload)=>sent.push({kind,payload})})
for(const type of ['session.execution.succeeded','permission.asked','form.created','session.execution.failed']) await adapter.event({type,data:{sessionID:'root',requestID:'q'}})
assert.deepEqual(sent.map(x=>x.kind),['completed','approval','input','failed'])
await adapter.event({type:'session.execution.succeeded',data:{sessionID:'child'}})
await adapter.event({type:'form.created',id:'e-form',data:{form:{sessionID:'root',id:'question'}}})
assert.equal(sent.at(-1).payload.request_id,'question')
await adapter.event({type:'session.status',data:{sessionID:'root',status:{type:'idle'}}})
assert.equal(sent.length,5)
let ignoredLookups=0
const quiet=createAdapter({getSession:async()=>{ignoredLookups++;return {}},forward:()=>{throw Error('must ignore lifecycle noise')}})
for(const type of ['session.execution.started','session.execution.interrupted','permission.replied','form.replied','form.cancelled','session.status']) await quiet.event({type,data:{sessionID:'root'}})
assert.equal(ignoredLookups,0)
await createAdapter({getSession:async()=>{throw Error('gone')},forward:()=>{throw Error('must not send')}}).event({type:'permission.asked',data:{sessionID:'s'}})
console.log('Agent native plugins: PASS (waterfall identity, errors, root filter, V2 mapping, idle suppression)')

const {normalizeGo,createQuotaCollector}=await import('../../macos/agents/opencode/quota.mjs')
let clock=1790000000000,calls=0,delivered=[]
const meter=(percent,status='ok')=>({percent,status,resetsAt:new Date(clock+86400000).toISOString()})
const go={usage:{rolling:meter(25),weekly:meter(100,'rate-limited'),monthly:meter(0)}}
assert.deepEqual(normalizeGo(go,'org-private',clock).items.map(x=>x.remaining),[75,0,100])
assert.equal(normalizeGo({usage:{...go.usage,rolling:meter(true)}},'org',clock),undefined)
assert.equal(normalizeGo({usage:{...go.usage,monthly:{...meter(10),resetsAt:'invalid'}}},'org',clock),undefined)
const integration={connection:{active:async()=>({id:'selected'}),resolve:async()=>({type:'oauth',access:'synthetic-test-token',metadata:{orgID:'org-test'}})}}
const fetcher=async(url,options)=>{calls++;assert.equal(url,'https://opencode.ai/zen/go/v1/usage');assert.equal(options.redirect,'error');assert.equal(options.headers['x-opencode-org-id'],'org-test');return {ok:true,text:async()=>JSON.stringify(go)}}
const collector=createQuotaCollector({integration,fetcher,clock:()=>clock,forward:x=>delivered.push(x)})
collector.activity();await new Promise(r=>setImmediate(r))
assert.equal(calls,1);assert.equal(delivered.length,1)
assert.equal(JSON.stringify(delivered).includes('org-test'),false);assert.equal(JSON.stringify(delivered).includes('synthetic-test-token'),false)
collector.activity();await collector.tick();assert.equal(calls,1)
clock+=300000;await collector.tick();assert.equal(calls,2)
clock+=300000;await collector.tick();assert.equal(calls,2) // idle ten minutes
collector.close();collector.activity();await collector.tick();assert.equal(calls,2)
let badCalls=0
const bad=createQuotaCollector({integration,clock:()=>clock,fetcher:async()=>{badCalls++;return {ok:false,headers:new Map()}}})
bad.activity();await new Promise(r=>setImmediate(r));clock+=300000;await bad.tick();assert.equal(badCalls,1)
clock+=300000;await bad.tick();assert.equal(badCalls,1) // idle prevents another query
bad.activity();await new Promise(r=>setImmediate(r));assert.equal(badCalls,2)
bad.close()
const unsafe=createQuotaCollector({integration:{connection:{active:async()=>({}),resolve:async()=>({type:'key',key:'private',metadata:{orgID:'org',server:'https://other.example'}})}},fetcher:async()=>{throw Error('credential must not leave allowed origin')},forward:()=>{throw Error('must not send')}})
unsafe.activity();await new Promise(r=>setImmediate(r));unsafe.close()
console.log('OpenCode Go quota: PASS (three real windows, zero, selected login, cadence, expiry, retry, bounded destination)')
