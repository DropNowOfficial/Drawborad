const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const html=fs.readFileSync('cloud/static/index.html','utf8'),els={},calls=[];
const by=id=>els[id]??=(id==='nativeColor'?{value:'#367c88'}:{});
const c=vm.createContext({by,nativeRun:f=>f(),nativeShow:async()=>{},nativePost:async(p,b)=>calls.push(JSON.parse(JSON.stringify(b)))});
vm.runInContext(html.match(/\/\/ BUCKET_HELPERS_BEGIN([\s\S]*?)\/\/ BUCKET_HELPERS_END/)[1],c);
vm.runInContext("let nativeBusy=false,nativeSelectedNodeId='color',nativeState={id:'p',revision:'3',capabilities:['bucket_fill','duplicate_paint_layer'],strokes:[{layer_id:'color'}],nodes:[{id:'root',kind:0,locked:false,visible:true},{id:'color',parent_id:'root',kind:3,locked:false,visible:true,source_kind:2,object_id:'00000000000000000000000000000000',children:[]}]};",c);
vm.runInContext(html.match(/function nativeDuplicateLayerReady\(\)\{[^\n]+/)[0],c);
vm.runInContext(html.match(/by\('nativeDuplicateLayer'\)\.onclick=[^\n]+/)[0],c);
(async()=>{assert.equal(c.nativeDuplicateLayerReady(),true);await by('nativeDuplicateLayer').onclick();assert.deepEqual(calls[0],{expected_revision:'3',type:'duplicate_paint_layer',layer_id:'color'});
for(const change of ["nativeBusy=true","nativeBusy=false;nativeState.nodes[0].locked=true","nativeState.nodes[0].locked=false;nativeState.capabilities=['bucket_fill']","nativeState.capabilities=['bucket_fill','duplicate_paint_layer'];nativeState.strokes=[]"]){vm.runInContext(change,c);assert.equal(c.nativeDuplicateLayerReady(),false);await by('nativeDuplicateLayer').onclick();assert.equal(calls.length,1);}
console.log('PASS hidden layer duplicate actual handler, parent lock, empty selection, capability/busy guards');})();
