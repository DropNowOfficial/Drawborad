"""Real native-engine + Chromium viewport integration; synthetic test canvas only."""
import hashlib, io, json, os, socket, subprocess, sys, time, urllib.request
from PIL import Image
from browser_native_acceptance import click_post, endpoint
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[2]
def exercise_poster(page, out):
 # All painting/fill/correction gestures below use actual page controls.
 state=page.evaluate('()=>nativeState');ink=state['strokes'][0]['layer_id']
 fill=next(n['id'] for n in state['nodes'] if n['kind']==3 and n['id']!=ink)
 def select(layer):page.locator('[data-node-id="'+layer+'"] .native-node-select').click()
 def pixels():
  pid=page.evaluate('()=>nativeState.id');r=page.request.get('/api/v1/projects/'+pid+'/preview.png')
  assert r.status==200
  im=Image.open(io.BytesIO(r.body())).convert('RGBA');assert im.size==(3300,5100)
  return im
 def digest():return hashlib.sha256(pixels().tobytes()).hexdigest()
 def screen(x,y):
  page.locator('#nativeCanvasHost').scroll_into_view_if_needed()
  return page.evaluate('p=>{const r=by("nativePreview").getBoundingClientRect();return {x:r.left+p.x*2,y:r.top+p.y*2}}',{'x':x,'y':y})
 def stroke(points):
  coords=[screen(x,y) for x,y in points]
  with page.expect_response(endpoint('POST','/commands')) as response:
   page.mouse.move(**coords[0]);page.mouse.down()
   for p in coords[1:]:page.mouse.move(**p,steps=6)
   page.mouse.up()
  assert response.value.status==200,response.value.text()
  page.wait_for_function('()=>!nativeBusy')
 select(ink);page.locator('#nativePen').check();page.locator('#nativePenWidth').fill('3')
 stroke([(1590,2500),(1710,2500),(1710,2600),(1590,2600),(1590,2500)])
 outline=digest();blank=pixels().getpixel((100,100));edge=pixels().getpixel((1650,2500))
 select(fill);page.locator('#nativeColor').evaluate("e=>{e.value='#c020b0';e.dispatchEvent(new Event('input',{bubbles:true}))}")
 page.locator('#nativeTolerance').fill('0');page.locator('#nativeBucket').check()
 point=screen(1650,2550)
 with page.expect_response(endpoint('POST','/commands')) as response:page.mouse.click(**point)
 assert response.value.status==200,response.value.text();page.wait_for_function('()=>!nativeBusy')
 event=response.value.json()['strokes'][-1];assert event['tool']=='bucket' and event['layer_id']==fill
 filled=digest();assert filled!=outline
 im=pixels();assert im.getpixel((1650,2550))[:3]==(192,32,176)
 assert im.getpixel((100,100))==blank and im.getpixel((1650,2500))==edge
 click_post(page,page.locator('#nativeUndo'),'/undo');assert digest()==outline
 click_post(page,page.locator('#nativeRedo'),'/redo');assert digest()==filled
 # Correct a mistaken mark with Undo, then draw the intended line.
 select(ink);page.locator('#nativeColor').evaluate("e=>{e.value='#000000';e.dispatchEvent(new Event('input',{bubbles:true}))}")
 page.locator('#nativePen').check();stroke([(1610,2540),(1690,2560)])
 assert digest()!=filled
 click_post(page,page.locator('#nativeUndo'),'/undo');assert digest()==filled
 stroke([(1610,2540),(1650,2530),(1690,2540)])
 final=digest();assert final!=filled and pixels().getpixel((1650,2530))==(0,0,0,255)
 state=page.evaluate('()=>nativeState')
 click_post(page,page.locator('#nativeCheckpoint'),'/checkpoint')
 assert page.evaluate('()=>nativeState.revision')==state['revision']
 with page.expect_download() as d:page.locator('#nativeRecoveryDownload').click()
 path=out/'poster-recovery.draw';d.value.save_as(path)
 saved=click_post(page,page.locator('#nativeSaveCopy'),'/save-copy',201)
 assert digest()==final and not saved['dirty']
 page.locator('#nativeFile').set_input_files(path)
 restored=click_post(page,page.locator('#nativeImport'),'/import',201)
 assert restored['strokes']==state['strokes'] and digest()==final
 pixels().save(out/'poster-native.png')
 return dict(native_size=[3300,5100],zoom='200%',actual_pointer_outline=True,actual_pointer_fill=True,
             fill_preserves_ink_and_exterior=True,undo_then_correct=True,save_copy_and_recovery_reopen_equal=True,
             limits=['Synthetic marks, not artwork-quality approval','Correction uses Undo and redraw, not point editing'])

def main():
 engine=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
 with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
 url=f'http://127.0.0.1:{port}';poster='--poster' in sys.argv[3:];width,height=(3300,5100) if poster else (1024,1024)
 env=dict(os.environ,DRAWBORAD_NATIVE_ENGINE=str(engine),DRAWBORAD_CLOUD_DATA=str(out/'data'),DRAWBORAD_CLOUD_TOKEN='')
 page=None
 with (out/'server.log').open('w') as log:
  server=subprocess.Popen([sys.executable,'-m','cloud.cli','serve','--host','127.0.0.1','--port',str(port)],cwd=ROOT,env=env,stdout=log,stderr=log)
  try:
   for _ in range(100):
    if server.poll() is not None:raise RuntimeError('server exited; see server.log')
    try:
     with urllib.request.urlopen(url+'/healthz',timeout=1) as response:
      if response.status==200:break
    except OSError:time.sleep(.1)
   else:raise RuntimeError('server did not become ready')
   with sync_playwright() as pw:
    browser=pw.chromium.launch(executable_path=os.getenv("DRAWBORAD_TEST_BROWSER") or None);page=browser.new_page(viewport={'width':1400,'height':1200},base_url=url);errors=[]
    page.on('pageerror',lambda e:errors.append(str(e)))
    page.goto(url)
    page.set_default_timeout(90000)
    page.evaluate("""async size=>{let s=await nativePost('',{width:String(size.width),height:String(size.height)});s=await nativePost('/'+s.id+'/commands',{expected_revision:s.revision,type:'add_fill_layer'});await nativeShow(s);nativeSelectedNodeId=s.nodes.find(n=>n.kind===3).id;nativeControls();}""",{'width':width,'height':height})
    page.locator('#nativeZoom').select_option('2');page.locator('#nativePan').check()
    page.locator('#nativeCanvasHost').scroll_into_view_if_needed();r=page.locator('#nativeCanvasHost').bounding_box();x=r['x']+r['width']/2;y=r['y']+200
    page.mouse.move(x,y);page.mouse.down();page.mouse.move(x+100,y+50);page.mouse.up()
    v=page.evaluate('()=>({x:nativeViewX,y:nativeViewY,busy:nativeBusy,count:nativeState.stroke_count,w:by("nativePreview").getBoundingClientRect().width})')
    assert v==dict(x=100,y=50,busy=False,count='0',w=width*2),v
    page.locator('#nativePan').uncheck();page.locator('#nativePen').check();page.locator('#nativePenWidth').fill('3')
    # Controls may scroll the page: remeasure after returning to the viewport.
    page.locator('#nativeCanvasHost').scroll_into_view_if_needed();r=page.locator('#nativeCanvasHost').bounding_box();x=r['x']+r['width']/2;y=r['y']+200
    expected=page.evaluate('p=>{const r=by("nativePreview").getBoundingClientRect();return {x:(p.x+20-r.left)/2,y:(p.y+15-r.top)/2}}',{'x':x,'y':y})
    page.mouse.move(x,y);page.mouse.down();page.mouse.move(x+40,y+30,steps=4)
    page.wait_for_function('()=>{const c=by("nativeStrokeDraft");return !c.hidden&&c.getContext("2d").getImageData(0,0,c.width,c.height).data.some((v,i)=>i%4===3&&v>0)}')
    assert page.evaluate('()=>nativeState.stroke_count')=='0','temporary preview must not commit a stroke'
    page.mouse.up()
    page.wait_for_function("()=>!nativeBusy&&nativeState.stroke_count==='1'")
    assert page.evaluate('()=>nativeViewX')==100
    assert page.locator('#nativeZoom').input_value()=='2'
    pixels=page.evaluate('p=>{const im=by("nativePreview"),c=document.createElement("canvas");c.width=im.naturalWidth;c.height=im.naturalHeight;const ctx=c.getContext("2d");ctx.drawImage(im,0,0);return {ink:Array.from(ctx.getImageData(Math.floor(p.x),Math.floor(p.y),1,1).data),blank:Array.from(ctx.getImageData(10,10,1,1).data)}}',expected)
    assert pixels['ink']!=pixels['blank'],pixels

    page.locator('#nativePan').check();page.locator('#nativeCanvasHost').scroll_into_view_if_needed();r=page.locator('#nativeCanvasHost').bounding_box()
    page.mouse.move(r['x']+r['width']/2,r['y']+200);page.mouse.down();page.evaluate("()=>window.dispatchEvent(new Event('blur'))")
    assert page.evaluate('()=>nativeBusy') is False
    page.mouse.up()
    page.locator('#nativePan').uncheck()
    page.locator('#nativeAddFillLayer').click();page.wait_for_function('()=>!nativeBusy&&nativeState.nodes.filter(n=>n.kind===3).length===2')
    old_order=page.evaluate('()=>{const r=nativeState.nodes.find(n=>n.kind===0);nativeSelectNode(r.children[0]);return r.children}')
    page.locator('#nativeLayerUp').click();page.wait_for_function('()=>!nativeBusy')
    assert page.evaluate('()=>nativeState.nodes.find(n=>n.kind===0).children')==list(reversed(old_order))
    page.locator('#nativeUndo').click();page.wait_for_function('()=>!nativeBusy')
    assert page.evaluate('()=>nativeState.nodes.find(n=>n.kind===0).children')==old_order
    checkpoint_before=page.evaluate('()=>({revision:nativeState.revision,dirty:nativeState.dirty})')
    page.locator('#nativeCheckpoint').click();page.wait_for_function('()=>!nativeBusy&&by("nativeRecoveryStatus").textContent.startsWith("Recovery saved")')
    assert page.evaluate('()=>({revision:nativeState.revision,dirty:nativeState.dirty})')==checkpoint_before
    with page.expect_download() as download_info:page.locator('#nativeRecoveryDownload').click()
    download_info.value.save_as(out/'recovery.draw');assert (out/'recovery.draw').stat().st_size>0
    poster_receipt=exercise_poster(page,out) if poster else None
    page.screenshot(path=str(out/'viewport.png'),full_page=True)
    assert not errors,errors
    receipt=dict(passed=True,synthetic=True,zoom='200%',pan=[100,50],live_draft_before_commit=True,stroke_committed=True,expected_document_pixel_has_ink=True,view_preserved=True,blur_unlock=True,layer_order_ui_undo=True,recovery_checkpoint_download=True,limitations=['Not artwork-quality approval','MyPaint/fill/eyedropper end-to-end coverage still separate'])
    if poster_receipt:receipt['poster_workflow']=poster_receipt
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2));print(json.dumps(receipt));browser.close()
  except Exception:
   if page is not None and not page.is_closed():
    try:page.screenshot(path=str(out/'failure.png'),full_page=True)
    except Exception:pass
   raise
  finally:
   server.terminate()
   try:server.wait(timeout=10)
   except subprocess.TimeoutExpired:server.kill();server.wait()
if __name__=='__main__':main()
