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
  focus(){}
}
async function fixture(scan,saved={networks:[{ssid:'Saved test',preferred:true}],capacity:5}){
  const elements=new Map();
  const element=id=>{if(!elements.has(id)){const e=new Element();e.id=id;elements.set(id,e)}return elements.get(id)};
  const state={scan,saved};
  const context=vm.createContext({
    document:{getElementById:element,createElement:tag=>{const e=new Element();e.tag=tag;return e}},
    Option:Element,AbortController,TextEncoder,setTimeout,clearTimeout,window:{close(){}},confirm:()=>false,
    fetch:async path=>{
      const value=path==='/status'?{connected:false}:path==='/saved'?state.saved:state.scan;
      return {ok:!value.error,json:async()=>value};
    }
  });
  vm.runInContext(script,context);await new Promise(setImmediate);
  return {element,state,refresh:async()=>{await vm.runInContext('scan()',context)}};
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
console.log('Phone portal UI: saved/nearby separation, failed versus empty scan, refresh recovery and retained saved list PASS');
