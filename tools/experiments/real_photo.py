"""CC0 photo + guided GrabCut; explicitly no SAM inference."""
import hashlib,json,sys
from pathlib import Path
import cv2
import numpy as np
from PIL import Image
from skimage import data
root=Path(sys.argv[1])
if root.exists(): raise ValueError('Output already exists')
a=cv2.resize(data.chelsea(),(192,128),interpolation=cv2.INTER_AREA)
# Photo-specific foreground/background hints, not automatic semantic inference.
outline=np.array([[10,0],[162,0],[156,28],[151,53],[151,75],[164,94],[145,118],[117,127],[0,127],[0,38]],np.int32)
# Exercise the reusable entry using the same explicit hints as this regression.
import importlib.util,tempfile
module_path=Path(__file__).resolve().parents[1]/'segmentation/guided_grabcut.py'
spec=importlib.util.spec_from_file_location('guided_grabcut',module_path)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
with tempfile.TemporaryDirectory() as temp:
    temp=Path(temp)
    Image.fromarray(a).save(temp/'source.png')
    seeds=np.full(a.shape[:2],85,np.uint8)
    cv2.fillPoly(seeds,[outline],170)
    cv2.ellipse(seeds,(76,62),(46,43),0,0,360,255,-1)
    seeds[:,177:]=0
    Image.fromarray(seeds).save(temp/'seeds.png')
    module.segment(temp/'source.png',temp/'seeds.png',root,'Cat candidate - needs review')
provenance=json.loads((root/'PROVENANCE.json').read_text())
provenance.update(source='skimage.data.chelsea',author='Stefan van der Walt',license='CC0',url='https://scikit-image.org/docs/stable/api/skimage.data.html#skimage.data.chelsea')
(root/'PROVENANCE.json').write_text(json.dumps(provenance,indent=2))
print('Reusable guided segmentation completed on the public real photograph.')
