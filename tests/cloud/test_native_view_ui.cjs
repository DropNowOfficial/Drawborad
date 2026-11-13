const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const html=fs.readFileSync('cloud/static/index.html','utf8');
const block=html.match(/\/\/ NATIVE_VIEW_HELPERS_BEGIN([\s\S]*?)\/\/ NATIVE_VIEW_HELPERS_END/);assert.ok(block,'native view layout helpers must exist');
const c=vm.createContext({});vm.runInContext(block[1],c);
let r=c.nativeViewLayout(640,560,1725,2625,'fit',0,0);assert.ok(Math.abs(r.scale-560/2625)<1e-12);assert.equal(r.height,560);
r=c.nativeViewLayout(640,560,1725,2625,'1',0,0);assert.equal(r.width,1725);assert.equal(r.height,2625);assert.equal(r.scale,1);
r=c.nativeViewLayout(640,560,1725,2625,'4',1e9,-1e9);assert.equal(r.width,6900);assert.equal(r.left,0);assert.equal(r.top,560-10500);
assert.equal(c.nativeViewLayout(640,560,0,2625,'1',0,0),null);assert.equal(c.nativeViewLayout(640,560,1725,2625,'bad',0,0),null);
console.log('PASS native fit/1CSS-pixel/400% layout and reachable pan limits');
const listeners={},win={},els={};let rpc=0;
function el(id){return els[id]??=(id==='nativeCanvasHost'?{getBoundingClientRect:()=>({left:0,top:0,right:640,bottom:560,width:640,height:560}),style:{},addEventListener:(k,f)=>listeners[k]=f,setPointerCapture:()=>{},hasPointerCapture:()=>false}:{value:id==='nativeZoom'?'2':'',checked:id==='nativePan',style:{},getBoundingClientRect:()=>({left:-704,top:-744,width:2048,height:2048})});}
const live=vm.createContext({by:el,window:{addEventListener:(k,f)=>win[k]=f},nativeState:{width:1024,height:1024},nativeBusy:false,nativeControls:()=>{},nativeBoundaryOverlay:()=>{},nativePenCancel:()=>{},nativeBoundaryCancel:()=>{},nativePost:()=>rpc++});
vm.runInContext(html.slice(html.indexOf('// NATIVE_VIEW_HELPERS_BEGIN'),html.indexOf('// BUCKET_HELPERS_BEGIN')),live);
listeners.pointerdown({pointerId:1,clientX:100,clientY:100,button:0,preventDefault(){}});assert.equal(live.nativeBusy,true);
listeners.pointermove({pointerId:1,clientX:200,clientY:150,preventDefault(){}});assert.equal(vm.runInContext('nativeViewX',live),100);assert.equal(vm.runInContext('nativeViewY',live),50);
listeners.pointerup({pointerId:2});assert.equal(live.nativeBusy,true,'second pointer cannot end first pan');listeners.pointerup({pointerId:1});assert.equal(live.nativeBusy,false);assert.equal(rpc,0,'viewport actions never edit document');
const boundary=html.match(/function nativeBoundaryPoint\(e\)\{[^\n]+/)[0];live.nativePenPoint=()=>({x:1,y:1});vm.runInContext(boundary,live);assert.equal(live.nativeBoundaryPoint({clientX:700,clientY:100}),null,'hidden image region is not editable');assert.ok(live.nativeBoundaryPoint({clientX:200,clientY:100}));
assert.match(html,/window.addEventListener\('blur',\(\)=>\{nativeEndPan\(\);/);
console.log('PASS actual pan handlers: gesture identity, no RPC, boundary clip and blur cleanup wiring');
