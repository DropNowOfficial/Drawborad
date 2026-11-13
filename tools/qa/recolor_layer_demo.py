"""Synthetic native drawing to visualize whole-layer recolor; no user artwork."""
import json, subprocess, sys
from pathlib import Path
from PIL import Image, ImageDraw
binary=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
p=subprocess.Popen([str(binary),'session'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);seq=0

def ok(method,**params):
 global seq
 seq+=1;p.stdin.write(json.dumps(dict(request_id=str(seq),method=method,params=params))+'\n');p.stdin.flush();r=json.loads(p.stdout.readline());assert 'result' in r,r;return r['result']
def add(s):
 old={n['id'] for n in s['nodes']};s=ok('command',type='add_fill_layer',expected_revision=s['revision']);return s,next(n['id'] for n in s['nodes'] if n['id'] not in old and n['kind']==3)
try:
 s=ok('create',width='480',height='480');s,ink=add(s)
 paths=[[(240,320),(180,300),(125,255),(100,190),(105,130),(160,140),(215,180),(245,235),(240,320)],[(242,245),(250,180),(300,120),(370,90),(380,150),(355,205),(300,240),(242,245)],[(236,380),(242,320),(242,245),(285,185)]]
 for path in paths:s=ok('command',type='draw_stroke',layer_id=ink,points=[dict(x=x,y=y)for x,y in path],width=3,color='#172d34',expected_revision=s['revision'])
 s,color=add(s)
 for x,y in [(170,210),(320,170)]:s=ok('command',type='bucket_fill',layer_id=color,x=x,y=y,tolerance=0,color='#c86647',expected_revision=s['revision'])
 ok('export',path=str(out/'before.png'),format='png')
 s=ok('command',type='recolor_layer',layer_id=color,color='#368a87',expected_revision=s['revision']);ok('export',path=str(out/'after.png'),format='png')
 s=ok('save_copy',path=str(out/'editable-leaves.draw'),expected_revision=s['revision']);ok('export',path=str(out/'editable-leaves.ora'),format='ora')
 a=Image.open(out/'before.png').convert('RGBA');b=Image.open(out/'after.png').convert('RGBA');assert a.getchannel('A').tobytes()==b.getchannel('A').tobytes()
 # Presentation only; all artwork pixels were exported by the native engine.
 board=Image.new('RGB',(1000,540),'#f5f2eb');board.paste(a,(10,40),a);board.paste(b,(510,40),b);d=ImageDraw.Draw(board);d.text((28,18),'BEFORE: two filled regions',fill='#172d34');d.text((528,18),'AFTER: one layer recolor',fill='#172d34');d.text((28,510),'Same outlines / geometry / alpha. Original native layers remain editable.',fill='#172d34');board.save(out/'comparison.png')
 print('native demo exported, geometry alpha identical; private references not used')
finally:p.stdin.close();p.wait(timeout=10)
