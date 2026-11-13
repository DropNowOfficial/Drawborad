const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync('cloud/static/index.html','utf8');
const source=html.match(/\/\/ BUCKET_HELPERS_BEGIN([\s\S]*?)\/\/ BUCKET_HELPERS_END/);
assert.ok(source,'bucket helpers must be implemented');
const context=vm.createContext({});
vm.runInContext(source[1],context);
const state={id:'one',revision:'4',width:'100',height:'50',stroke_count:'2',capabilities:['bucket_fill'],
 nodes:[{id:'root',parent_id:'',kind:0,locked:false,visible:true},{id:'obj',parent_id:'root',kind:1,locked:false,visible:true},{id:'layer',parent_id:'obj',kind:3,locked:false,visible:true}]};
assert.equal(context.nativeFillTarget(state,'obj'),null);
assert.equal(context.nativeFillTarget(state,null),null);
assert.equal(context.nativeFillTarget(state,'layer').id,'layer');
state.nodes[1].locked=true;assert.equal(context.nativeFillTarget(state,'layer'),null);state.nodes[1].locked=false;
state.nodes[1].visible=false;assert.equal(context.nativeFillTarget(state,'layer'),null);state.nodes[1].visible=true;
const rect={left:10,top:20,width:200,height:200};
assert.equal(context.nativeFillPoint(rect,100,50,110,40),null,'letterbox is not paint');
assert.equal(JSON.stringify(context.nativeFillPoint(rect,100,50,110,120)),JSON.stringify({x:50,y:25}));
assert.equal(context.nativeFillPoint(rect,100,50,210,120),null,'right edge excluded');
assert.equal(context.nativeFillPoint(rect,100,50,110,170),null,'bottom edge excluded');
assert.equal(context.nativeFillPoint(rect,0,50,110,120),null);
const ready=html.match(/function nativeFillReady\(\)\{[\s\S]*?\n\}/)[0];
const run=html.match(/async function nativeRun\(action\)\{[\s\S]*?\n\}/)[0];
const click=html.match(/by\('nativePreview'\)\.onclick=event=>\{[\s\S]*?\n\};/)[0];
const add=html.match(/async function nativeAddFillLayer\(\)\{[\s\S]*?\n\}/);
assert.ok(add,'new color layer handler must exist');
assert.match(html, /id="nativeInkCover" type="checkbox" disabled>/, 'edge coverage defaults off');
const elements={nativeBoundaryMode:{value:'off'},nativeEyedropper:{checked:false},nativeInkCover:{checked:false},nativeBucket:{checked:true},nativeTolerance:{value:'0'},
 nativeColor:{value:'#aabbcc'},nativePreview:{getBoundingClientRect:()=>rect}};
let resolvePost,calls=[];
Object.assign(context,{nativeState:state,nativeSelectedNodeId:'layer',nativeBusy:false,
 nativePreviewKey:'one:4:2',by:id=>elements[id],nativeControls:()=>{},nativeError:()=>{},
 nativeShow:async()=>{},nativePost:async(path,body)=>{calls.push({path,body});return new Promise(resolve=>{resolvePost=resolve})}});
vm.runInContext(ready+'\n'+run+'\n'+click,context);
(async()=>{
 context.nativePreviewKey='one:3:2';
 elements.nativePreview.onclick({clientX:110,clientY:120});
 assert.equal(calls.length,0,'stale preview cannot fill');
 context.nativePreviewKey='one:4:1';
 elements.nativePreview.onclick({clientX:110,clientY:120});
 assert.equal(calls.length,0,'partial replay cannot fill');
 context.nativePreviewKey='one:4:2';
 elements.nativePreview.onclick({clientX:110,clientY:120});
 elements.nativePreview.onclick({clientX:110,clientY:120});
 assert.equal(calls.length,1,'busy prevents duplicate fill');
 assert.equal(JSON.stringify(calls[0].body),JSON.stringify({expected_revision:'4',type:'bucket_fill',layer_id:'layer',x:50,y:25,tolerance:0,color:'#aabbcc'}));
 resolvePost({});await new Promise(resolve=>setImmediate(resolve));
 assert.equal(context.nativeBusy,false);
 elements.nativeInkCover.checked=true;
 elements.nativePreview.onclick({clientX:110,clientY:120});
 assert.equal(calls.length,2);
 assert.equal(calls[1].body.under_ink,2,'explicit opt-in adds bounded under-ink request');
 resolvePost({});await new Promise(resolve=>setImmediate(resolve));
 elements.nativeInkCover.checked=false;
 context.nativePost=async(path,body)=>{
  assert.equal(JSON.stringify(body),JSON.stringify({expected_revision:'4',type:'add_fill_layer'}));
  return {...state,revision:'5',nodes:[...state.nodes,{id:'new-color',parent_id:'root',kind:3,locked:false,visible:true}]};
 };
 elements.nativeShowSublayers={checked:false};
 let selected=null;
 context.nativeRenderSelection=()=>{selected=context.nativeSelectedNodeId};
 context.nativeShow=async value=>{context.nativeState=value};
 vm.runInContext(add[0],context);
 await context.nativeAddFillLayer();
 assert.equal(elements.nativeShowSublayers.checked,true);
 assert.equal(selected,'new-color','new independent color layer is selected');
 console.log('bucket UI helpers, stale/replay guards and duplicate-click tests passed');
})().catch(error=>{console.error(error);process.exitCode=1});
