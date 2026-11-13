"""Real native-engine + Chromium viewport integration; synthetic test canvas only."""
import json, os, socket, subprocess, sys, time, urllib.request
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[2]
def main():
 engine=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
 with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
 url=f'http://127.0.0.1:{port}'
 env=dict(os.environ,DRAWBORAD_NATIVE_ENGINE=str(engine),DRAWBORAD_CLOUD_DATA=str(out/'data'),DRAWBORAD_CLOUD_TOKEN='')
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
    browser=pw.chromium.launch();page=browser.new_page(viewport={'width':1400,'height':1200});errors=[]
    page.on('pageerror',lambda e:errors.append(str(e)))
    page.goto(url)
    page.evaluate("""async()=>{let s=await nativePost('',{width:'1024',height:'1024'});s=await nativePost('/'+s.id+'/commands',{expected_revision:s.revision,type:'add_fill_layer'});await nativeShow(s);nativeSelectedNodeId=s.nodes.find(n=>n.kind===3).id;nativeControls();}""")
    page.locator('#nativeZoom').select_option('2');page.locator('#nativePan').check()
    page.locator('#nativeCanvasHost').scroll_into_view_if_needed();r=page.locator('#nativeCanvasHost').bounding_box();x=r['x']+r['width']/2;y=r['y']+200
    page.mouse.move(x,y);page.mouse.down();page.mouse.move(x+100,y+50);page.mouse.up()
    v=page.evaluate('()=>({x:nativeViewX,y:nativeViewY,busy:nativeBusy,count:nativeState.stroke_count,w:by("nativePreview").getBoundingClientRect().width})')
    assert v==dict(x=100,y=50,busy=False,count='0',w=2048),v
    page.locator('#nativePan').uncheck();page.locator('#nativePen').check();page.locator('#nativePenWidth').fill('3')
    # Controls may scroll the page: remeasure after returning to the viewport.
    page.locator('#nativeCanvasHost').scroll_into_view_if_needed();r=page.locator('#nativeCanvasHost').bounding_box();x=r['x']+r['width']/2;y=r['y']+200
    expected=page.evaluate('p=>{const r=by("nativePreview").getBoundingClientRect();return {x:(p.x+20-r.left)/2,y:(p.y+15-r.top)/2}}',{'x':x,'y':y})
    page.mouse.move(x,y);page.mouse.down();page.mouse.move(x+40,y+30,steps=4);page.mouse.up()
    page.wait_for_function("()=>!nativeBusy&&nativeState.stroke_count==='1'")
    assert page.evaluate('()=>nativeViewX')==100
    assert page.locator('#nativeZoom').input_value()=='2'
    pixels=page.evaluate('p=>{const im=by("nativePreview"),c=document.createElement("canvas");c.width=1024;c.height=1024;const ctx=c.getContext("2d");ctx.drawImage(im,0,0);return {ink:Array.from(ctx.getImageData(Math.floor(p.x),Math.floor(p.y),1,1).data),blank:Array.from(ctx.getImageData(10,10,1,1).data)}}',expected)
    assert pixels['ink']!=pixels['blank'],pixels

    page.locator('#nativePan').check();page.locator('#nativeCanvasHost').scroll_into_view_if_needed();r=page.locator('#nativeCanvasHost').bounding_box()
    page.mouse.move(r['x']+r['width']/2,r['y']+200);page.mouse.down();page.evaluate("()=>window.dispatchEvent(new Event('blur'))")
    assert page.evaluate('()=>nativeBusy') is False
    page.mouse.up()
    page.screenshot(path=str(out/'viewport.png'),full_page=True)
    assert not errors,errors
    receipt=dict(passed=True,synthetic=True,zoom='200%',pan=[100,50],stroke_committed=True,expected_document_pixel_has_ink=True,view_preserved=True,blur_unlock=True,limitations=['Not artwork-quality approval','MyPaint/fill/eyedropper end-to-end coverage still separate'])
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2));print(json.dumps(receipt));browser.close()
  finally:
   server.terminate()
   try:server.wait(timeout=10)
   except subprocess.TimeoutExpired:server.kill();server.wait()
if __name__=='__main__':main()
