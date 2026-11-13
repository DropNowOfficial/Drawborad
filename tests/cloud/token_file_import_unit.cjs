// Periodic callbacks are controlled by dedicated recovery tests, not wall-clock timers here.
/* Synthetic credentials only. Socket-free DOM contract tests, not browser QA. */
const assert = require('node:assert/strict');
// node:buffer File supports Node 18.13+ without relying on global File (Node 20+).
const {File, Blob} = require('node:buffer');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const html = fs.readFileSync(path.join(__dirname, '../../cloud/static/index.html'), 'utf8');
const elements = new Map();
for (const [,id] of html.matchAll(/\bid="([^"]+)"/g)) elements.set(id, {
  value:'', files:[], disabled:false, textContent:'', style:{}, listeners:{},
  classList:{toggle(){},remove(){}},
  addEventListener(type, fn){(this.listeners[type] ||= []).push(fn)},
  dispatchEvent(event){this['on'+event.type]?.(event);for(const fn of this.listeners[event.type]||[]) fn(event)},
});
const get = id => elements.get(id), requests = [], storage = [];
const context = vm.createContext({
  document:{getElementById:get}, window:{addEventListener(){}}, Event,
  localStorage:{getItem(){return null},setItem(...args){storage.push(args)}},
  fetch:async (url,opts)=>{requests.push({url,opts});return {ok:false,status:401,headers:{get:()=> 'application/json'},json:async()=>({detail:'Bearer token required'})}},
  setInterval:()=>1,clearInterval(){},console,
});
vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1],context);
const tick = () => new Promise(resolve=>setImmediate(resolve));
const candidate = 'synthetic-test-only_A-123';
async function choose(text,size=Buffer.byteLength(text)){
 get('tokenFile').files=[{size,text:async()=>text}];await get('tokenFile').onchange();await tick();
}
(async()=>{
 await tick();requests.length=0;
 assert.ok(get('tokenFile'), 'Explicit local token-file selection is missing');
 await choose(candidate+'\r\n');
 assert.equal(get('tokenCandidate').value,candidate);
 assert.equal(get('token').value,'');assert.equal(requests.length,0);
 // Even unrelated/background requests cannot send a staged credential.
 await vm.runInContext("api('/capabilities').catch(()=>{})",context);
 assert.equal(requests[0].opts.headers.Authorization,undefined);requests.length=0;
 get('tokenFileApply').onclick();await tick();
 assert.equal(get('token').value,candidate);assert.equal(get('tokenCandidate').value,'');
 assert.equal(requests.length,1);assert.equal(requests[0].url,'/api/v1/capabilities');
 assert.equal(requests[0].opts.headers.Authorization,'Bearer '+candidate);
 requests.length=0;get('tokenFileApply').onclick();await tick();assert.equal(requests.length,0);
 for(const invalid of ['', '   ', 'a\nb', 'a\rb', 'a\tb', 'a\0b', 'a\u007fb', 'é', 'token\n\n', 'x'.repeat(4097)]){
  await choose(invalid);assert.equal(get('tokenCandidate').value,'');assert.equal(get('tokenFileApply').disabled,true);
  assert.equal(get('token').value,candidate);assert.equal(requests.length,0);
 }
 let read=false;get('tokenFile').files=[{size:4097,text:async()=>{read=true;return candidate}}];
 await get('tokenFile').onchange();assert.equal(read,false);
 await choose('x'.repeat(4096));assert.equal(get('tokenCandidate').value.length,4096);
 get('tokenFileClear').onclick();assert.equal(get('tokenCandidate').value,'');
 let release;get('tokenFile').files=[{size:4,text:()=>new Promise(resolve=>release=resolve)}];
 const pending=get('tokenFile').onchange();get('tokenFileClear').onclick();release('old1');await pending;
 assert.equal(get('tokenCandidate').value,'');assert.equal(get('tokenFileApply').disabled,true);
 get('tokenFile').files=[{size:4,text:()=>new Promise(resolve=>release=resolve)}];
 const older=get('tokenFile').onchange();await choose('new2');release('old1');await older;
 assert.equal(get('tokenCandidate').value,'new2');
 get('tokenFile').files=[];await get('tokenFile').onchange();assert.equal(get('tokenCandidate').value,'');
 await choose(candidate);get('tokenFile').dispatchEvent(new Event('cancel'));assert.equal(get('tokenCandidate').value,'');
 await choose(candidate);get('tokenFilePanel').open=false;get('tokenFilePanel').dispatchEvent(new Event('toggle'));assert.equal(get('tokenCandidate').value,'');
 await choose(candidate);get('token').value='synthetic-manual';get('token').dispatchEvent(new Event('input'));
 assert.equal(get('tokenCandidate').value,'');
 get('tokenFile').files=[{size:4,text:async()=>{throw new Error('private filename must not be shown')}}];
 await get('tokenFile').onchange();assert.equal(get('tokenFileStatus').textContent.includes('private filename'),false);
 // Exercise the real in-memory File/Blob UTF-8 decoder, not a fake text().
 for(const file of [new File([candidate],'synthetic.fixture'),new Blob([candidate+'\r\n']),new File([new Uint8Array([0xef,0xbb,0xbf]),candidate],'synthetic-bom.fixture')]){
  get('tokenFile').files=[file];await get('tokenFile').onchange();
  assert.equal(get('tokenCandidate').value,candidate);
 }
 const bomAtLimit=new File([new Uint8Array([0xef,0xbb,0xbf]),'x'.repeat(4093)],'synthetic-limit.fixture');
 assert.equal(bomAtLimit.size,4096);get('tokenFile').files=[bomAtLimit];await get('tokenFile').onchange();
 assert.equal(get('tokenCandidate').value.length,4093);
 for(const file of [new Blob([new Uint8Array([0xff,0xfe,0x41])]),new File([new Uint8Array([0xef,0xbb,0xbf]),'x'.repeat(4094)],'synthetic-oversize.fixture')]){
  get('tokenFile').files=[file];await get('tokenFile').onchange();
  assert.equal(get('tokenCandidate').value,'');assert.equal(get('tokenFileApply').disabled,true);
 }
 assert.equal(storage.length,0);assert.equal(requests.length,0);
 console.log('PASS: synthetic import, explicit apply, no early transmission/token storage, rejection, cancellation, races, read errors, real memory File/Blob UTF-8 BOM decoding');
})().catch(error=>{console.error(error);process.exitCode=1});
