// Execute the real inline portal script against a minimal DOM/fetch fixture.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const script=readFileSync(new URL('../../main/vibe/provisioning.html',import.meta.url),'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
class Element {
  constructor(text='',value=''){this.textContent=text;this._value=value;this.children=[];this.dataset={};this.classList={toggle:(name,on)=>{this[name]=on}}}
  get options(){return this.children.flatMap(child=>child.tag==='optgroup'?child.children:[child])}
  get selectedOptions(){const options=this.options;return [options.filter(option=>option.selected).at(-1)||options[0]].filter(Boolean)}
  get value(){return this.id==='network'?(this.selectedOptions[0]?.value||''):this._value}
  set value(value){this._value=value}
  replaceChildren(){this.children=[]}
  add(option){this.children.push(option)}
  append(option){this.children.push(option)}
  focus(){this.focused=true}
  blur(){this.blurred=true}
  scrollIntoView(){this.scrolled=true}
  setAttribute(name,value){this[name]=value}
}
async function fixture(scan,saved={networks:[{ssid:'Saved test',preferred:true}],capacity:5},initial={}){
  const elements=new Map();
  const element=id=>{if(!elements.has(id)){const e=new Element();e.id=id;elements.set(id,e)}return elements.get(id)};
  const state={scan,saved,status:{connected:false},save:{ok:true},calls:[],...initial};
  element('connectionProgress').hidden=true;
  const keyboard=new Element();
  const context=vm.createContext({
    document:{getElementById:element,activeElement:keyboard,createElement:tag=>{const e=new Element();e.tag=tag;return e}},
    Option:Element,AbortController,TextEncoder,setTimeout,clearTimeout,window:{close(){}},confirm:()=>false,
    fetch:async path=>{
      state.calls.push(path);
      const selected=path==='/status'?state.status:path==='/saved'?state.saved:path==='/save'?state.save:state.scan;
      const value=typeof selected==='function'?await selected():selected;
      return {ok:!value.error,json:async()=>value};
    }
  });
  vm.runInContext(script,context);await new Promise(setImmediate);
  return {element,state,keyboard,refresh:async()=>{await vm.runInContext('scan()',context)}};
}
const busy=await fixture({error:'scan_busy'});
assert(busy.element('network').options.some(option=>option.value==='Saved test'));
assert(busy.element('status').error && busy.element('status').textContent.includes('正在连接'));
busy.state.scan=[{ssid:'New test',rssi:-40}];await busy.refresh();
assert(busy.element('network').children.some(group=>group.label==='附近的 Wi-Fi'&&group.children.some(option=>option.value==='New test')));
assert(!busy.element('status').error);
busy.state.saved={error:'unavailable'};await busy.refresh();
assert(busy.element('network').options.some(option=>option.value==='Saved test'));
assert(busy.element('network').options.some(option=>option.value==='New test'));
assert(busy.element('status').textContent.includes('已保存列表读取失败'));
const empty=await fixture([]);
assert(empty.element('status').textContent.includes('未找到附近的 2.4 GHz'));
assert(!empty.element('status').error && empty.element('network').options.some(option=>option.value==='Saved test'));
for(const error of ['scan_failed','scan_memory']){
  const f=await fixture({error});
  assert(f.element('status').error);
  assert(f.element('network').options.some(option=>option.value==='Saved test'));
  assert(f.element('network').options.some(option=>option.dataset.manual));
}
const first=await fixture([],{networks:[],capacity:5});
assert(first.element('network').selectedOptions[0].dataset.manual);
assert(!first.element('ssid').hidden);
// Delay the POST itself: feedback must appear before the device acknowledges it.
const submit=await fixture([{ssid:'New test',rssi:-40}],{networks:[],capacity:5});
for(const option of submit.element('network').options)option.selected=option.value==='New test';
submit.element('network').onchange();submit.element('password').value='12345678';
let accepted;submit.state.save=()=>new Promise(resolve=>{accepted=resolve});
submit.state.status={connected:true};
const pending=submit.element('form').onsubmit({preventDefault(){}});
assert(submit.element('form').hidden && !submit.element('connectionProgress').hidden);
assert(!submit.element('connectionSpinner').hidden && submit.element('connectedMark').hidden);
assert.equal(submit.element('connectionTitle').textContent,'正在连接 Wi-Fi');
assert.equal(submit.element('form')['aria-busy'],'true');
assert(submit.keyboard.blurred && submit.element('connectionProgress').focused && submit.element('connectionProgress').scrolled);
await submit.element('form').onsubmit({preventDefault(){}});
assert.equal(submit.state.calls.filter(path=>path==='/save').length,1);
accepted({ok:true});await pending;
assert(submit.element('connectionSpinner').hidden && !submit.element('connectedMark').hidden);
assert.equal(submit.element('connectionTitle').textContent,'Wi-Fi 已连接');
assert(submit.element('form').hidden && submit.element('connect').disabled);
assert.equal(submit.element('password').value,'');

// Both rejected submissions and device connection failures return to a usable form.
for(const postError of [true,false]){
  const failed=await fixture([]);
  failed.state.save=postError?{error:'busy'}:{ok:true};
  failed.state.status={failed:true,message:'密码不正确，请重试。'};
  await failed.element('form').onsubmit({preventDefault(){}});
  assert(!failed.element('form').hidden && failed.element('connectionProgress').hidden);
  assert(!failed.element('connect').disabled && failed.element('status').error);
  assert.equal(failed.element('form')['aria-busy'],'false');
  assert.equal(failed.element('connect').textContent,'连接 Wi-Fi');
  failed.state.save={ok:true};failed.state.status={connected:true};
  await failed.element('form').onsubmit({preventDefault(){}});
  assert(!failed.element('connectedMark').hidden && failed.element('connect').disabled);
}

// Reloading an in-progress page shows the same waiting state and retains errors.
let finishPoll,reads=0;
const resume=await fixture([],{networks:[{ssid:'Saved test'}]},
  {status:()=>++reads===1?{connecting:true}:new Promise(resolve=>{finishPoll=resolve})});
assert(!resume.element('connectionProgress').hidden && resume.element('form').hidden);
finishPoll({failed:true,message:'连接失败，请重试。'});
await new Promise(setImmediate);
assert(!resume.element('form').hidden && resume.element('connectionProgress').hidden);
assert.equal(resume.element('status').textContent,'连接失败，请重试。');
assert(resume.element('status').error && resume.element('network').options.some(option=>option.value==='Saved test'));
console.log('Phone portal UI: visible submit/resume progress, keyboard dismissal, duplicate prevention, failure/retry, success and network lists PASS');
