"""Synthetic sample for cloud smoke tests. NOT a model-generated segmentation."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


def make_demo(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=False)
    (output / 'masks').mkdir()
    w, h = 640, 400
    source = Image.new('RGB', (w,h), '#d6bb94')
    dr = ImageDraw.Draw(source)
    for y in range(0,400,18):
        dr.line((0,y,w,y+30),fill='#c5a682', width=2)
    masks=[]
    # Crisp visual regions: saucer, red cup, metal spoon. Farther background is wood.
    for draw_mask in (
        lambda m: ImageDraw.Draw(m).ellipse((135,228,505,360), fill=255),
        lambda m: (ImageDraw.Draw(m).ellipse((208,123,454,300), fill=255),
                   ImageDraw.Draw(m).ellipse((223,108,423,179), fill=255)),
        lambda m: (ImageDraw.Draw(m).line((431,105,559,306), fill=255,width=15),
                   ImageDraw.Draw(m).ellipse((427,82,468,123),fill=255))
    ):
        m=Image.new('L',(w,h),0)
        draw_mask(m)
        masks.append(np.asarray(m)>0)
    # Source appearance is composed in visible order; background layer only
    # contains unoccluded pixels after ownership resolution.
    for mask,color in zip(masks,('#b94f51','#efeded','#9da6ae')):
        color_im=Image.new('RGB',(w,h),color)
        source.paste(color_im,mask=Image.fromarray(np.uint8(mask)*255))
    dr=ImageDraw.Draw(source)
    dr.ellipse((240,128,405,169),fill='#68432b')
    # Coffee surface is still part of the *cup* object.
    masks[1] |= np.asarray(Image.new('L',(w,h),0))>0
    assigned=np.zeros((h,w),dtype=bool)
    named=[]
    for label,foreground,score in zip(['spoon','cup','saucer'],[masks[2],masks[1],masks[0]],[.96,.95,.92]):
        visible=foreground & ~assigned
        assigned|=visible
        named.append((label,visible,score,False))
    wood=~assigned
    # Source RGB originally has an opaque wood base; preserve coffee color
    # while the cup mask remains its owner.
    layers=[('wooden table',wood,1.0,True)]+list(reversed(named))
    # Write alpha-safe RGBA cutouts as raw 0/255 mask bytes.
    source_path=output/'source.png'
    source.save(source_path)
    objs=[]
    for i,(name,mask,score,bg) in enumerate(layers):
        b=np.where(mask,255,0).astype(np.uint8).tobytes()
        path=f'masks/layer-{i:02d}.u8'
        (output/path).write_bytes(b)
        objs.append({'name':name,'score':score,'mask':path,'sha256':hashlib.sha256(b).hexdigest(),
                     'pixels':int(mask.sum()),'isBackground':bg})
    payload={'schema':1,'backend':'fixture-no-ai','promptOrder':'back-to-front',
             'width':w,'height':h,'image':'source.png',
             'sourceSha256':hashlib.sha256(source_path.read_bytes()).hexdigest(),
             'prompts':['wooden table','saucer','cup','spoon'],
             'objects':objs,'automaticSemanticAccuracyVerified':False,
             'containsInpaintedOcclusion':False}
    (output/'manifest.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2)+'\n')