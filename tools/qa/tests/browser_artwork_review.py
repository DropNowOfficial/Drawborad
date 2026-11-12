"""Browser behavior test on labeled software input; not native/art quality proof."""
import json,sys
from pathlib import Path
from PIL import Image,ImageFilter
from playwright.sync_api import sync_playwright
from tools.qa.artwork_review import write_review
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
a=Image.new('RGB',(160,120),'white')
for x in range(20,130,4):a.paste('black',(x,20,x+1,100))
a.save(out/'fixture.png');a.filter(ImageFilter.GaussianBlur(1)).save(out/'blur.png')
write_review(out/'fixture.png',out/'blur.png',[{'name':'Software detail fixture — not artwork','box':[10,10,140,110]}],out/'review')
with sync_playwright() as p:
    browser=p.chromium.launch(headless=True);page=browser.new_page(viewport={'width':1280,'height':900})
    external=[];page.on('request',lambda r:external.append(r.url) if r.url.startswith(('http:','https:')) else None)
    page.goto((out/'review/index.html').as_uri())
    page.wait_for_function('Array.from(document.images).every(i=>i.complete&&i.naturalWidth>0)')
    assert page.locator('section').count()==2
    image=page.locator('img').first
    assert image.evaluate('(i)=>i.width')==160
    page.get_by_role('button',name='200% · display only').click();assert image.evaluate('(i)=>i.width')==320
    page.get_by_role('button',name='100% · original pixels').click();assert image.evaluate('(i)=>i.width')==160
    assert 'not_reviewed' in page.locator('body').inner_text()
    assert not external,external
    page.screenshot(path=str(out/'review.png'),full_page=True)
    browser.close()
(out/'browser.json').write_text(json.dumps({'browser':'passed','input':'software fixture, not native artwork','external_requests':external,'zoom_roundtrip':'160→320→160'})+'\n')
