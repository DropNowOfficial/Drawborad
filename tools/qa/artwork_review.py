"""Pixel-aligned visual evidence; never certifies art, geometry or source detail."""
from __future__ import annotations
import argparse,base64,hashlib,html,io,json,shutil
from pathlib import Path
import numpy as np
from PIL import Image,ImageOps

MAX_PIXELS=4*1024*1024

def _rgba(image):
    if image.mode not in ('RGB','RGBA','L','LA'):
        raise ValueError('Only 8-bit RGB/RGBA/grayscale comparisons are supported')
    return np.asarray(image.convert('RGBA'),dtype=np.uint16)

def _background(a,white):
    alpha=a[:,:,3:4]
    return ((a[:,:,:3]*alpha+(255-alpha)*(255 if white else 0)+127)//255).astype(np.int16)

def _measure(a,b):
    aw,bw=_background(a,True),_background(b,True)
    delta=bw-aw
    error=np.maximum(np.abs(delta),np.abs(_background(b,False)-_background(a,False)))
    # Neighbor differences are diagnostics in encoded sample space, not SSIM/DeltaE.
    ea=[];eb=[];changes=[]
    for axis in (0,1):
        if a.shape[axis]>1:
            da=np.diff(aw,axis=axis);db=np.diff(bw,axis=axis)
            ea.append(np.abs(da).reshape(-1));eb.append(np.abs(db).reshape(-1))
            changes.append(np.abs(db-da).reshape(-1))
    mean=lambda arrays:float(np.concatenate(arrays).mean()) if arrays else 0.0
    return dict(mean_visible_error_u8=float(error.mean()),p95_visible_error_u8=float(np.percentile(error,95)),max_error_u8=int(error.max()),alpha_max_error_u8=int(np.abs(b[:,:,3].astype(np.int16)-a[:,:,3]).max()),darkening_mean_u8=float(np.maximum(-delta,0).mean()),brightening_mean_u8=float(np.maximum(delta,0).mean()),reference_edge_energy=mean(ea),candidate_edge_energy=mean(eb),edge_change_mean_u8=mean(changes))

def compare(reference,candidate,regions):
    if reference.size!=candidate.size:raise ValueError('Dimensions differ; no implicit alignment or resizing')
    w,h=reference.size
    if w*h>MAX_PIXELS or min(w,h)<1:raise ValueError('Comparison exceeds 4 Mi-pixel review budget')
    if not isinstance(regions,list) or len(regions)>32:raise ValueError('At most 32 named critical regions')
    names=set()
    for r in regions:
        if not isinstance(r,dict) or not isinstance(r.get('name'),str) or not 1<=len(r['name'])<=128 or r['name'] in names:raise ValueError('Distinct region names required')
        box=r.get('box')
        if not isinstance(box,list) or len(box)!=4 or any(type(n)!=int for n in box):raise ValueError('Region uses integer x0,y0,x1,y1 coordinates')
        x,y,x1,y1=box
        if not (0<=x<x1<=w and 0<=y<y1<=h):raise ValueError('Region outside source; no clamping')
        names.add(r['name'])
    a,b=_rgba(reference),_rgba(candidate)
    pa,pb=reference.info.get('icc_profile'),candidate.info.get('icc_profile')
    profile='profile_mismatch' if pa!=pb else ('same_profile_encoded_samples' if pa else 'untagged_encoded_samples')
    rows=[]
    for r in regions:
        x,y,x1,y1=r['box'];rows.append(dict(name=r['name'],box=r['box'],**_measure(a[y:y1,x:x1],b[y:y1,x:x1])))
    return dict(schema=1,width=w,height=h,coordinate_space='EXIF-oriented source pixels; no resampling',color_comparison=profile,metric_space='8-bit encoded samples; worst RGB error over black and white composites; edge/bias diagnostics on white',whole=_measure(a,b),regions=rows,artistic_acceptance='not_reviewed',native_editability='not_verified',authentic_detail_recovery='not_verified',critical_regions_provided=bool(rows))

def _png(image):
    b=io.BytesIO();image.save(b,format='PNG');return 'data:image/png;base64,'+base64.b64encode(b.getvalue()).decode()

def _panel(a,b,name,metric):
    ar,br=_rgba(a),_rgba(b)
    diff=np.maximum(np.abs(_background(ar,True)-_background(br,True)),np.abs(_background(ar,False)-_background(br,False))).astype(np.uint8)
    figures=''.join('<figure><figcaption>'+label+'</figcaption><div class="pixels"><img alt="'+label+'" src="'+_png(im)+'"></div></figure>' for label,im in [('Reference',a),('Native render / candidate',b),('Absolute difference · 1×',Image.fromarray(diff))])
    return '<section><h2>'+html.escape(name)+'</h2><p>Mean error '+format(metric['mean_visible_error_u8'],'.3f')+' / 255 · edge change '+format(metric['edge_change_mean_u8'],'.3f')+' · darkening '+format(metric['darkening_mean_u8'],'.3f')+'</p><div class="grid">'+figures+'</div></section>'

def _input_bytes(path):
    with path.open('rb') as f:data=f.read(32*1024*1024+1)
    if len(data)>32*1024*1024:raise ValueError('Input exceeds 32 MiB')
    if data.startswith(b'\x89PNG\r\n\x1a\n'):
        if len(data)<29 or data[12:16]!=b'IHDR' or data[24]!=8:raise ValueError('Only encoded 8-bit PNG/JPEG is supported')
    elif data.startswith(b'\xff\xd8'):
        # Validate JPEG SOF sample precision before Pillow can reduce it.
        i=2;precision=None
        while i<len(data):
            if data[i]!=255:break
            while i<len(data) and data[i]==255:i+=1
            if i>=len(data):break
            marker=data[i];i+=1
            if marker in (0xd9,0xda):break
            if marker==1 or 0xd0<=marker<=0xd7:continue
            if i+2>len(data):break
            length=int.from_bytes(data[i:i+2],'big')
            if length<2 or i+length>len(data):break
            if 0xc0<=marker<=0xcf and marker not in (0xc4,0xc8,0xcc):
                if length>=3:precision=data[i+2]
                break
            i+=length
        if precision!=8:raise ValueError('Only encoded 8-bit PNG/JPEG is supported')
    else:raise ValueError('Only encoded 8-bit PNG/JPEG is supported')
    return data

def write_review(source,render,regions,out):
    source,render,out=Path(source),Path(render),Path(out)
    if out.exists():raise FileExistsError('Review output already exists; preserve prior evidence')
    source_bytes,render_bytes=_input_bytes(source),_input_bytes(render)
    with Image.open(io.BytesIO(source_bytes)) as s,Image.open(io.BytesIO(render_bytes)) as c:
        # Check metadata dimensions before decoding/transposing an oversized input.
        if s.width*s.height>MAX_PIXELS or c.width*c.height>MAX_PIXELS:raise ValueError('Comparison exceeds 4 Mi-pixel review budget')
        a,b=ImageOps.exif_transpose(s),ImageOps.exif_transpose(c)
        report=compare(a,b,regions)
        report['inputs']={role:dict(name=p.name,sha256=hashlib.sha256(data).hexdigest()) for role,p,data in [('reference',source,source_bytes),('candidate',render,render_bytes)]}
        panels=_panel(a,b,'Whole image',report['whole'])
        for r in report['regions']:panels+=_panel(a.crop(r['box']),b.crop(r['box']),r['name'],r)
    page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Drawborad artwork review</title><style>
body{margin:0;padding:32px;background:#f5f5f7;color:#1d1d1f;font:16px/1.5 system-ui}main{max-width:1400px;margin:auto}h1{font-size:36px}section{background:white;padding:24px;border-radius:16px;margin:24px 0}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}figure{margin:0;min-width:0}figcaption{margin:8px 0;color:#555}.pixels{overflow:auto;max-height:600px;background:repeating-conic-gradient(#ddd 0% 25%,white 0% 50%) 0/16px 16px}img{display:block;max-width:none}button{padding:10px 20px;margin:8px;border:0;border-radius:8px;background:#0071e3;color:white}pre{white-space:pre-wrap;overflow-wrap:anywhere}small{color:#555}@media(max-width:700px){.grid{grid-template-columns:1fr}body{padding:16px}}
</style><main><h1>Inspect the detail, not just the score.</h1><p>Original-size evidence. No resizing, inferred detail or automatic artistic approval.</p><p><strong>Artistic acceptance: not_reviewed · Native editability: not_verified</strong></p><p>Color comparison: PROFILE. These are encoded-pixel diagnostics, not perceptual color accuracy or proof of native provenance.</p><p>This private report embeds both inputs. Keep it private.</p><button data-zoom="1">100% · original pixels</button><button data-zoom="2">200% · display only</button> PANELS <details><summary>Reproducible measurements and input hashes</summary><pre>REPORT</pre></details></main><script>
document.querySelectorAll('button[data-zoom]').forEach(button=>button.addEventListener('click',()=>{document.querySelectorAll('img').forEach(img=>{img.style.width=(img.naturalWidth*Number(button.dataset.zoom))+'px';img.style.imageRendering=button.dataset.zoom==='1'?'auto':'pixelated'})}));
</script></html>'''
    before,after=page.split(' PANELS ')
    page=before.replace('PROFILE',html.escape(report['color_comparison']))+panels+after.replace('REPORT',html.escape(json.dumps(report,indent=2,ensure_ascii=False)))
    out.parent.mkdir(parents=True,exist_ok=True);out.mkdir()
    try:
        (out/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n')
        (out/'index.html').write_text(page)
    except Exception:
        shutil.rmtree(out);raise
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--render',type=Path,required=True);p.add_argument('--regions',type=Path,required=True,help='JSON list of named original-pixel boxes');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    report=write_review(a.source,a.render,json.loads(a.regions.read_text()),a.output)
    print(json.dumps(dict(output=str(a.output),artistic_acceptance=report['artistic_acceptance'],regions=len(report['regions']))))
if __name__=='__main__':main()
