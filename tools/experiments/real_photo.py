"""CC0 photo + guided GrabCut; explicitly no SAM inference."""
import hashlib,json,sys
from pathlib import Path
import cv2
import numpy as np
from PIL import Image
from skimage import data
root=Path(sys.argv[1]);root.mkdir(parents=True,exist_ok=True)
(root/'masks').mkdir(exist_ok=True)
a=cv2.resize(data.chelsea(),(192,128),interpolation=cv2.INTER_AREA)
Image.fromarray(a).save(root/'source.png')
mask=np.zeros(a.shape[:2],np.uint8)
cv2.setRNGSeed(0)
cv2.grabCut(cv2.cvtColor(a,cv2.COLOR_RGB2BGR),mask,(18,5,158,119),np.zeros((1,65)),np.zeros((1,65)),5,cv2.GC_INIT_WITH_RECT)
fg=np.isin(mask,[1,3]);assert fg.any() and (~fg).any()
objects=[]
for i,(name,m) in enumerate([('Background visible region',~fg),('Cat candidate - needs review',fg)]):
    b=(m.astype(np.uint8)*255).tobytes();p=f'masks/layer-{i:02d}.u8';(root/p).write_bytes(b)
    Image.fromarray(np.dstack([a,m.astype(np.uint8)*255])).save(root/f'object-{i}.png')
    objects.append(dict(name=name,score=1,isBackground=i==0,pixels=int(m.sum()),mask=p,sha256=hashlib.sha256(b).hexdigest()))
manifest=dict(schema=1,backend='opencv-grabcut',promptOrder='back-to-front',image='source.png',sourceSha256=hashlib.sha256((root/'source.png').read_bytes()).hexdigest(),width=192,height=128,objects=objects)
(root/'manifest.json').write_text(json.dumps(manifest,indent=2))
(root/'PROVENANCE.json').write_text(json.dumps(dict(source='skimage.data.chelsea',author='Stefan van der Walt',license='CC0',url='https://scikit-image.org/docs/stable/api/skimage.data.html#skimage.data.chelsea',method='OpenCV GrabCut rectangle-guided; no SAM',rectangle=[18,5,158,119],resized=[192,128],quality='Unreviewed candidate; visible regions only; no occluded-object completion',scoreMeaning='Import acceptance weight, not calibrated segmentation confidence'),indent=2))
assert np.array_equal(np.where(fg[...,None],a,0)+np.where((~fg)[...,None],a,0),a)
print('Real photograph segmented into two complementary transparent layers.')
