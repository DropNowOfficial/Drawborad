const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const html=fs.readFileSync('cloud/static/index.html','utf8'),els={},calls=[];
const by=id=>els[id]??=(id==='nativeColor'?{value:'#367c88'}:{});
const c=vm.createContext({by,nativeRun:f=>f(),nativeShow:async()=>{},nativePost:async(p,b)=>calls.push(JSON.parse(JSON.stringify(b)))});
vm.runInContext(html.match(/\/\/ BUCKET_HELPERS_BEGIN([\s\S]*?)\/\/ BUCKET_HELPERS_END/)[1],c);
vm.runInContext("let nativeBusy=false,nativeSelectedNodeId='color',nativeState={id:'p',revision:'3',capabilities:['bucket_fill','recolor_layer'],strokes:[{layer_id:'color'}],nodes:[{id:'root',kind:0,locked:false,visible:true},{id:'color',parent_id:'root',kind:3,locked:false,visible:true}]};",c);
vm.runInContext(html.match(/function nativeRecolorLayerReady\(\)\{[^\n]+/)[0],c);
vm.runInContext(html.match(/by\('nativeRecolorLayer'\)\.onclick=[^\n]+/)[0],c);
(async()=>{assert.equal(c.nativeRecolorLayerReady(),true);await by('nativeRecolorLayer').onclick();assert.deepEqual(calls[0],{expected_revision:'3',type:'recolor_layer',layer_id:'color',color:'#367c88'});
for(const change of ["nativeBusy=true","nativeBusy=false;nativeState.nodes[0].locked=true","nativeState.nodes[0].locked=false;nativeState.capabilities=['bucket_fill']","nativeState.capabilities=['bucket_fill','recolor_layer'];nativeState.strokes=[]"]){vm.runInContext(change,c);assert.equal(c.nativeRecolorLayerReady(),false);await by('nativeRecolorLayer').onclick();assert.equal(calls.length,1);}
console.log('PASS layer recolor actual handler, parent lock, empty selection, capability/busy guards');})();
