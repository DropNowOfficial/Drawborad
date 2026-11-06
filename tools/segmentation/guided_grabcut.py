"""Reusable assisted segmentation. No learned model or automatic object naming."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import cv2
import numpy as np
from PIL import Image, ImageOps

def segment(image_path, seeds_path, output, name='Foreground candidate'):
    output=Path(output)
    if output.exists(): raise ValueError('Output directory already exists')
    if not name or len(name)>128: raise ValueError('Object name must contain 1–128 characters')
    with Image.open(image_path) as im:
        im=ImageOps.exif_transpose(im)
        if im.width>4096 or im.height>4096 or im.width*im.height>4*1024*1024:
            raise ValueError('Image exceeds native input budget')
        if 'A' in im.getbands() and im.getchannel('A').getextrema()!=(255,255):
            raise ValueError('This assisted photo tool requires an opaque source')
        rgb=np.array(im.convert('RGB'))
    with Image.open(seeds_path) as im: seeds=np.array(im.convert('L'))
    if seeds.shape!=rgb.shape[:2]: raise ValueError('Seed image must match oriented source dimensions')
    if not np.isin(seeds,[0,85,170,255]).all(): raise ValueError('Seed labels must be 0,85,170,255')
    if np.count_nonzero(seeds==0)<5 or np.count_nonzero(seeds==255)<5:
        raise ValueError('At least five certain foreground and background pixels required')
    labels=np.zeros(seeds.shape,np.uint8)
    for value,label in [(0,cv2.GC_BGD),(85,cv2.GC_PR_BGD),(170,cv2.GC_PR_FGD),(255,cv2.GC_FGD)]: labels[seeds==value]=label
    cv2.setRNGSeed(0)
    cv2.grabCut(cv2.cvtColor(rgb,cv2.COLOR_RGB2BGR),labels,None,np.zeros((1,65)),np.zeros((1,65)),5,cv2.GC_INIT_WITH_MASK)
    fg=np.isin(labels,[1,3])
    if not fg.any() or fg.all(): raise ValueError('Segmentation must contain foreground and background')
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.grabcut-',dir=output.parent) as temp:
        root=Path(temp)/'result';root.mkdir();(root/'masks').mkdir()
        Image.fromarray(rgb).save(root/'source.png');Image.fromarray(seeds).save(root/'seeds.png')
        objects=[]
        for i,(label,m) in enumerate([('Background visible region',~fg),(name,fg)]):
            b=(m.astype(np.uint8)*255).tobytes();relative=f'masks/layer-{i:02d}.u8'
            (root/relative).write_bytes(b)
            Image.fromarray(np.dstack([rgb,m.astype(np.uint8)*255])).save(root/f'object-{i}.png')
            objects.append(dict(name=label,score=1,isBackground=i==0,pixels=int(m.sum()),mask=relative,sha256=hashlib.sha256(b).hexdigest()))
        manifest=dict(schema=1,backend='opencv-grabcut',promptOrder='back-to-front',image='source.png',sourceSha256=hashlib.sha256((root/'source.png').read_bytes()).hexdigest(),width=rgb.shape[1],height=rgb.shape[0],objects=objects)
        (root/'manifest.json').write_text(json.dumps(manifest,indent=2))
        (root/'PROVENANCE.json').write_text(json.dumps(dict(method='OpenCV GrabCut with user seed map; no SAM',opencvVersion=cv2.__version__,sourceFileName=Path(image_path).name,sourceFileSha256=hashlib.sha256(Path(image_path).read_bytes()).hexdigest(),seedLabels={'0':'certain background','85':'probable background','170':'probable foreground','255':'certain foreground'},quality='Needs review; visible pixels only; score is import weight, not confidence'),indent=2))
        root.rename(output)
    return manifest

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image',required=True);p.add_argument('--seeds',required=True);p.add_argument('--output',required=True);p.add_argument('--name',default='Foreground candidate')
    a=p.parse_args()
    try: print(json.dumps(segment(a.image,a.seeds,a.output,a.name)))
    except (ValueError,OSError,cv2.error) as e: p.exit(2,str(e)+'\n')
if __name__=='__main__': main()
