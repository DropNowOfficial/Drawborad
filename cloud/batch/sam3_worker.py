"""Real official SAM3 image adapter. Imports CUDA/model only inside the GPU job."""
import hashlib,os,time
from pathlib import Path
import numpy as np
from PIL import Image,ImageOps
from tools.models.sam3_semantic import Candidate,infer_candidates,build_semantic_partition,write_semantic_manifest

def project_candidate(candidate,view,shape):
    x,y,r,b=view['box']
    if candidate.mask.shape!=(b-y,r-x):raise ValueError('Model mask differs from its source view')
    mask=np.zeros(shape,dtype=bool);mask[y:b,x:r]=candidate.mask
    return Candidate(view['id']+'/'+candidate.name,candidate.score,mask)

def segment(source,output,job):
    import torch
    from sam3.model_builder import build_sam3_image_model
    from sam3.model.sam3_image_processor import Sam3Processor
    if not torch.cuda.is_available():raise RuntimeError('Actual CUDA device required for sam3-image job')
    checkpoint=Path(os.environ['SAM3_CHECKPOINT'])
    expected=os.environ['SAM3_CHECKPOINT_SHA256']
    if not checkpoint.is_file():raise RuntimeError('Licensed official image checkpoint must be provisioned privately')
    with checkpoint.open('rb') as f:sha=hashlib.file_digest(f,'sha256').hexdigest()
    if sha!=expected:raise ValueError('Checkpoint integrity mismatch')
    torch.cuda.reset_peak_memory_stats();started=time.monotonic()
    model=build_sam3_image_model(checkpoint_path=str(checkpoint),load_from_HF=False,device='cuda')
    processor=Sam3Processor(model,device='cuda',confidence_threshold=.5)
    torch.cuda.synchronize();loaded=time.monotonic()
    with Image.open(source) as raw:
        image=ImageOps.exif_transpose(raw).convert('RGB');alpha=np.asarray(ImageOps.exif_transpose(raw).convert('RGBA').getchannel('A'))
    views=job['views'] or [dict(id='image',box=[0,0,image.width,image.height],confirmed=True)]
    candidates=[]
    with torch.inference_mode():
        for view in views:
            found=infer_candidates(processor,image.crop(view['box']),job['prompts'])
            candidates.extend(project_candidate(c,view,(image.height,image.width)) for c in found)
    torch.cuda.synchronize();finished=time.monotonic()
    if not candidates:raise RuntimeError('SAM3 detected no matching objects; no fallback masks')
    layers=build_semantic_partition(candidates,(image.height,image.width),alpha)
    write_semantic_manifest(source,output,layers,'sam3-image',job['prompts'])
    return dict(model='official SAM3 image',checkpoint_sha256=sha,device=torch.cuda.get_device_name(),vram_bytes=str(torch.cuda.get_device_properties(0).total_memory),peak_vram_bytes=str(torch.cuda.max_memory_allocated()),model_load_seconds=loaded-started,inference_seconds=finished-loaded,worker_started_at=os.environ.get('DRAWBORAD_WORKER_STARTED_AT'),cold_start_seconds=None,gpu_billed_seconds=None,layout_source='user-confirmed-regions' if job['views'] else 'whole-image',automatic_layout=False)
