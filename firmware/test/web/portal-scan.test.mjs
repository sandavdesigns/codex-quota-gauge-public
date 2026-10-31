import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

// Run the actual embedded portal JavaScript, rather than a duplicate implementation.
function productionPortalLiterals() {
  const source=readFileSync(new URL('../../src/provisioning.cpp',import.meta.url),'utf8');
  const fn=source.split('void ProvisioningManager::sendPortal() {')[1].split('void ProvisioningManager::handleSave()')[0];
  return fn.split('\n').map(line=>{
    const match=line.match(/page \+= F\("((?:\\.|[^"\\])*)"\);/);
    return match ? JSON.parse('"'+match[1]+'"') : line.includes('page += csrfToken_;') ? 'a'.repeat(32) : '';
  }).join('');
}
function productionScript() {
  const html=productionPortalLiterals();
  const script=html.match(/<script>([\s\S]*?)<\/script>/)?.[1];
  assert.ok(script,'portal script present');return script;
}
function element() {
  return {textContent:'',value:'',disabled:false,children:[],listeners:{},
    replaceChildren(){this.children=[];},appendChild(node){this.children.push(node);},
    addEventListener(name,fn){this.listeners[name]=fn;},focus(){this.focused=true;},
    set innerHTML(_){throw new Error('Unsafe HTML rendering');}};
}
async function portal(initial) {
  const ids=Object.fromEntries(['networks','scanstatus','ssid','rescan','password','scheme','port'].map(x=>[x,element()]));
  ids.password.value='typed-password';
  ids.scheme.value='http';
  ids.port.value='8787';
  const timers=new Map(), calls=[];let next=0, reply=initial;
  const context={document:{getElementById:id=>ids[id],createElement:()=>element()},AbortController,URLSearchParams,
    setTimeout:(fn,ms)=>{const id=++next;timers.set(id,{fn,ms});return id;},clearTimeout:id=>timers.delete(id),
    fetch:async(url,options)=>{calls.push({url,options});return {ok:true,status:url==='/scan'?202:200,json:async()=>reply};}};
  vm.runInNewContext(productionScript(),context);
  const settle=()=>new Promise(resolve=>setImmediate(resolve));
  await settle();
  return {ids,timers,calls,settle,setReply:value=>reply=value};
}
test('visible networks are selectable as text; Enterprise disabled; password retained',async()=>{
  const p=await portal({status:'ready',networks:[
    {ssid:'<b>Demo</b>',rssi:-40,security:'geschützt',supported:true},
    {ssid:'Office Enterprise',rssi:-50,security:'Enterprise – nicht unterstützt',supported:false}]});
  assert.equal(p.ids.networks.children.length,2);
  assert.match(p.ids.networks.children[0].textContent,/<b>Demo<\/b>/);
  assert.equal(p.ids.networks.children[1].disabled,true);
  p.ids.networks.children[0].listeners.click();
  assert.equal(p.ids.ssid.value,'<b>Demo</b>');assert.equal(p.ids.ssid.focused,true);
  assert.equal(p.ids.password.value,'typed-password');
});
test('asynchronous results and re-scan update the list without losing form input',async()=>{
  const p=await portal({status:'running',networks:[]});
  assert.match(p.ids.scanstatus.textContent,/suchen/);
  p.ids.ssid.value='manually typed';
  p.setReply({status:'ready',networks:[]});
  const poll=[...p.timers.values()].find(x=>x.ms===1500);assert.ok(poll);
  await poll.fn();await p.settle();
  assert.match(p.ids.scanstatus.textContent,/Keine Netze/);
  assert.equal(p.ids.ssid.value,'manually typed');assert.equal(p.ids.password.value,'typed-password');
  p.setReply({status:'ready',networks:[{ssid:'Demo WLAN',rssi:-60,security:'offen',supported:true}]});
  await p.ids.rescan.listeners.click();await p.settle();
  const request=p.calls.find(x=>x.url==='/scan');assert.equal(request.options.method,'POST');
  assert.equal(new URLSearchParams(request.options.body).get('csrf'),'a'.repeat(32));
  assert.equal(p.ids.networks.children.length,1);assert.match(p.ids.networks.children[0].textContent,/Demo WLAN/);
  assert.equal(p.ids.password.value,'typed-password');
});
test('scheme switch selects HTTPS default but preserves a custom port',async()=>{
  const p=await portal({status:'ready',networks:[]});
  p.ids.scheme.value='https';
  p.ids.scheme.listeners.change();
  assert.equal(p.ids.port.value,'443');
  p.ids.port.value='8443';
  p.ids.scheme.value='http';
  p.ids.scheme.listeners.change();
  assert.equal(p.ids.port.value,'8443');
});
test('device token password is never prefilled into portal HTML',()=>{
  const html=productionPortalLiterals();
  const tokenInput=html.match(/<input name="token"[^>]*>/)?.[0];
  assert.ok(tokenInput,'token input present');
  assert.match(tokenInput,/type="password"/);
  assert.doesNotMatch(tokenInput,/\bvalue=/);
});
