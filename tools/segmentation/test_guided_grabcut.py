import tempfile
from pathlib import Path
import numpy as np
from PIL import Image
from guided_grabcut import segment
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);a=np.zeros((32,32,3),np.uint8);a[:]=[30,80,160];a[8:24,8:24]=[200,40,20]
    s=np.zeros((32,32),np.uint8);s[6:26,6:26]=170;s[12:20,12:20]=255
    Image.fromarray(a).save(p/'source.png');Image.fromarray(s).save(p/'seeds.png')
    r=segment(p/'source.png',p/'seeds.png',p/'output','Test object')
    assert r['backend']=='opencv-grabcut'
    layers=[np.array(Image.open(p/'output'/f'object-{i}.png')) for i in range(2)]
    assert np.all(layers[0][:,:,3].astype(int)+layers[1][:,:,3]==255)
    rebuilt=sum(np.where(x[:,:,3:]>0,x[:,:,:3],0).astype(int) for x in layers)
    assert np.array_equal(rebuilt,a)
    for bad in ['existing','labels','size','missing-seeds']:
        out=p/('output' if bad=='existing' else bad)
        b=s.copy()
        if bad=='labels': b[0,0]=99
        if bad=='size': b=b[:-1]
        if bad=='missing-seeds': b[:]=85
        Image.fromarray(b).save(p/'bad.png')
        try: segment(p/'source.png',p/'bad.png',out)
        except ValueError: pass
        else: raise AssertionError(bad)
        if bad!='existing': assert not out.exists()
print('Guided segmentation partition, exact pixel reconstruction, no-clobber and invalid-input tests passed')
