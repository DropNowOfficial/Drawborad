// Periodic callbacks are controlled by dedicated recovery tests, not wall-clock timers here.
/* Socket-free characterization of existing change handlers. Synthetic tokens only. */
const assert = require('node:assert/strict');
// node:buffer File supports Node 18.13+ without relying on global File (Node 20+).
const {File, Blob} = require('node:buffer');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../../cloud/static/index.html'), 'utf8');
const tick = () => new Promise(resolve => setImmediate(resolve));
const tokenA='synthetic-only-token-A', tokenB='synthetic-only-token-B';
const projectId='synthetic-project';
const json = (data, status=200) => ({ok:status===200,status,headers:{get:()=> 'application/json'},json:async()=>data});
const unauthorized = () => json({detail:'Bearer token required'},401);
const project = {id:projectId,revision:'1',dirty:false,width:'512',height:'512',stroke_count:'0',nodes:[],strokes:[]};
class Element {
 constructor(){this.value='';this.files=[];this.children=[];this.dataset={};this.style={};this.attributes={};this.listeners={};this.disabled=false;this.textContent='';this.classList={toggle(){},remove(){}}}
 addEventListener(type,fn){(this.listeners[type] ||= []).push(fn)}
 dispatchEvent(event){this['on'+event.type]?.(event);for(const fn of this.listeners[event.type]||[])fn(event)}
 replaceChildren(...children){this.children=children;this.value=''}
 setAttribute(key,value){this.attributes[key]=value}
 querySelectorAll(){return []}
 async decode(){
  assert.equal(this.src,'blob:synthetic-preview');
  await Promise.resolve();
  this.naturalWidth=512;this.naturalHeight=512;this.complete=true;
  this.decodeCalls=(this.decodeCalls||0)+1;
 }
 getBoundingClientRect(){return {left:0,top:0,width:512,height:512}}
}
function setup(){
 const elements=new Map([...html.matchAll(/\bid="([^"]+)"/g)].map(([,id])=>[id,new Element()]));
 const get=id=>elements.get(id), requests=[], writes=[], storage=new Map([['drawborad.project',projectId]]);
 const harness={get,requests,writes,storage,respond:()=>unauthorized()};
 const context=vm.createContext({document:{getElementById:get},window:{addEventListener(){}},Event,
  localStorage:{getItem:key=>storage.get(key)||null,setItem:(key,value)=>{writes.push([key,value]);storage.set(key,value)},removeItem:key=>storage.delete(key)},
  URL:{createObjectURL:()=> 'blob:synthetic-preview',revokeObjectURL(){}},setInterval:()=>1,clearInterval(){},console,
  fetch:async(url,opts={})=>{requests.push({url,opts});return harness.respond(url,opts)},
 });
 vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1],context);
 harness.apply=async token=>{
  get('tokenFile').files=[new File([token],'synthetic.fixture')];
  await get('tokenFile').onchange();get('tokenFileApply').onclick();await tick();
 };
 return harness;
}
function noCredentialPersistence(h){
 for(const write of h.writes)assert.deepEqual(write,['drawborad.project',projectId]);
 assert.deepEqual([...h.storage],[['drawborad.project',projectId]]);
}
function success(url){
 if(url.endsWith('/capabilities'))return json({model_ready:true,device:'synthetic-device'});
 if(url.endsWith('/preview.png'))return {ok:true,headers:{get:key=>key==='X-Drawborad-Revision'?project.revision:'image/png'},blob:async()=>new Blob(['synthetic preview'])};
 assert.equal(url,'/api/v1/projects/'+projectId);return json(project);
}
(async()=>{
 // A saved project reconnects and renders its preview; only its ID is persisted.
 let h=setup();await tick();h.requests.length=0;h.respond=success;
 await h.apply(tokenA);
 assert.deepEqual(h.requests.map(r=>r.url),['/api/v1/capabilities','/api/v1/projects/'+projectId,'/api/v1/projects/'+projectId+'/preview.png']);
 for(const r of h.requests)assert.equal(r.opts.headers.Authorization,'Bearer '+tokenA);
 assert.equal(h.get('nativePreview').src,'blob:synthetic-preview');assert.equal(h.get('nativePreview').hidden,false);
 assert.equal(h.get('nativePreview').decodeCalls,1);
 assert.equal(h.get('nativeError').textContent,'');
 assert.deepEqual(h.writes,[['drawborad.project',projectId]]);noCredentialPersistence(h);
 // Auth failure still labels the action Applied, never authenticated/successful.
 h=setup();await tick();h.requests.length=0;await h.apply(tokenA);
 assert.deepEqual(h.requests.map(r=>r.url),['/api/v1/capabilities','/api/v1/projects/'+projectId]);
 for(const r of h.requests)assert.equal(r.opts.headers.Authorization,'Bearer '+tokenA);
 assert.equal(h.get('deviceStatus').textContent,'Owner access token required');
 assert.equal(h.get('nativeError').textContent,'Bearer token required');
 assert.match(h.get('tokenFileStatus').textContent,/Applied to this page\. Check the connection status/);
 noCredentialPersistence(h);
 // Project load succeeds but preview fails: existing nativeRun retries state/preview.
 h=setup();await tick();h.requests.length=0;
 h.respond=url=>url.endsWith('/preview.png')?{ok:false,text:async()=> 'synthetic preview failure'}:success(url);
 await h.apply(tokenA);
 assert.deepEqual(h.requests.map(r=>r.url),['/api/v1/capabilities','/api/v1/projects/'+projectId,'/api/v1/projects/'+projectId+'/preview.png','/api/v1/projects/'+projectId,'/api/v1/projects/'+projectId+'/preview.png']);
 for(const r of h.requests)assert.equal(r.opts.headers.Authorization,'Bearer '+tokenA);
 assert.match(h.get('nativeStatus').textContent,/Disconnected/);assert.match(h.get('nativeError').textContent,/synthetic preview failure/);noCredentialPersistence(h);
 // Existing limitation: Apply while nativeBusy skips another reconnect; an old
 // in-flight project response can render after the new credential is selected.
 h=setup();await tick();h.requests.length=0;let releaseProject,releaseCapability;
 h.respond=(url,opts)=>{
  if(opts.headers.Authorization==='Bearer '+tokenA){
   if(url.endsWith('/capabilities'))return new Promise(resolve=>releaseCapability=resolve);
   if(url==='/api/v1/projects/'+projectId)return new Promise(resolve=>releaseProject=resolve);
  }
  return success(url);
 };
 await h.apply(tokenA);await h.apply(tokenB);
 assert.equal(h.get('token').value,tokenB);
 assert.equal(h.requests.filter(r=>r.url==='/api/v1/projects/'+projectId).length,1);
 assert.equal(h.get('deviceStatus').textContent,'SAM 3 configured · synthetic-device');
 releaseProject(json(project));await tick();
 assert.equal(h.get('nativePreview').src,'blob:synthetic-preview');
 assert.equal(h.requests.at(-1).opts.headers.Authorization,'Bearer '+tokenB);
 // Existing limitation: a stale capability failure can overwrite newer status.
 releaseCapability(unauthorized());await tick();
 assert.equal(h.get('deviceStatus').textContent,'Owner access token required');
 assert.equal(h.get('token').value,tokenB);noCredentialPersistence(h);
 console.log('PASS: saved-project Apply success/auth failure/preview failure, Bearer headers, project-only storage; existing busy/stale-response limits characterized');
})().catch(error=>{console.error(error);process.exitCode=1});

