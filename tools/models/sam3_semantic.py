#!/usr/bin/env python3
"""SAM 3 image text-prompt segmentation -> Drawborad native review manifest.

Uses the official facebookresearch/sam3 image API. No masks or semantic labels are
fabricated: each instance is an actual model output, or explicit unrecognized
background. The generated manifest is consumed by the Qt review/import workflow.
Model weights are not bundled. SAM 3.1 multiplex video checkpoints are NOT
silently passed off as equivalent SAM 3 image checkpoints.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import shutil
from typing import Any, Sequence

import numpy as np
from PIL import Image, ImageOps

MAX_PIXELS = 4 * 1024 * 1024
MAX_LAYERS = 64
MAX_SOURCE_BYTES = 32 * 1024 * 1024


@dataclass(frozen=True)
class Candidate:
    name: str
    score: float
    mask: np.ndarray  # [H, W] boolean, original image coordinates
    is_background: bool = False


def _numpy(value: Any) -> np.ndarray:
    if hasattr(value, "detach"):
        value = value.detach().cpu().numpy()
    return np.asarray(value)


def infer_candidates(processor: Any, image: Image.Image, prompts: Sequence[str],
                     min_score: float = 0.5, min_pixels: int = 8) -> list[Candidate]:
    """Run official processor for noun phrases in painter's back-to-front order."""
    if not prompts or len(prompts) > MAX_LAYERS - 1:
        raise ValueError("1..63 short semantic prompts required")
    state = processor.set_image(image)
    found: list[Candidate] = []
    for phrase in prompts:
        phrase = phrase.strip()
        if not phrase or len(phrase) > 128:
            raise ValueError("nonempty noun phrases must be <=128 characters")
        response = processor.set_text_prompt(state=state, prompt=phrase)
        masks = _numpy(response["masks"])
        scores = _numpy(response["scores"]).reshape(-1)
        if masks.ndim == 4 and masks.shape[1] == 1:
            masks = masks[:, 0]
        if masks.ndim != 3 or masks.shape[0] != scores.shape[0] or masks.shape[1:] != (image.height, image.width):
            raise ValueError(f"invalid SAM 3 output tensor shape for {phrase}")
        matches = sorted(zip(scores, masks), key=lambda x: -float(x[0]))
        instances: list[Candidate] = []
        for rank, (raw_score, raw_mask) in enumerate(matches, 1):
            score = float(raw_score)
            if not math.isfinite(score) or score < 0 or score > 1:
                raise ValueError("model reported invalid confidence")
            mask = np.asarray(raw_mask) > 0.5
            if score >= min_score and int(mask.sum()) >= min_pixels:
                name = phrase if len(matches) == 1 else f"{phrase} {rank}"
                instances.append(Candidate(name, score, mask))
        # The partition is resolved last-to-first. Within the *same* semantic
        # prompt, high confidence must win overlap, never the weaker instance.
        found.extend(reversed(instances))
        if len(found) > MAX_LAYERS - 1:
            raise ValueError("too many instances; narrow the prompts")
    return found


def build_semantic_partition(candidates: Sequence[Candidate], shape: tuple[int, int],
                             source_alpha: np.ndarray | None = None) -> list[Candidate]:
    """Convert overlapping object detections into disjoint visible regions.

    Later prompts/instances win overlapping pixels (frontmost); no occluded
    pixels are invented. Unrecognized visible pixels remain explicit background.
    """
    h, w = shape
    if h <= 0 or w <= 0 or h * w > MAX_PIXELS:
        raise ValueError("invalid image dimensions")
    if len(candidates) > MAX_LAYERS - 1:
        raise ValueError("too many detected objects")
    visible = np.ones(shape, dtype=bool) if source_alpha is None else np.asarray(source_alpha) > 0
    if visible.shape != shape:
        raise ValueError("source alpha dimension mismatch")
    assigned = np.zeros(shape, dtype=bool)
    kept: list[Candidate] = []
    for item in reversed(candidates):
        if not item.name or len(item.name) > 128:
            raise ValueError("invalid semantic label")
        if not math.isfinite(item.score) or not 0 <= item.score <= 1:
            raise ValueError("invalid score")
        if np.asarray(item.mask).shape != shape:
            raise ValueError("mask dimensions differ from source")
        mask = np.asarray(item.mask, dtype=bool) & visible & ~assigned
        if mask.any():
            kept.append(Candidate(item.name, item.score, mask.copy()))
            assigned |= mask
    kept.reverse()
    background = visible & ~assigned
    layers = kept
    if background.any():
        layers = [Candidate("Background (unrecognized)", 1.0, background, True)] + layers
    if not layers:
        raise ValueError("the image has no visible pixels")
    if len(layers) > MAX_LAYERS:
        raise ValueError("layer count exceeded")
    assert np.array_equal(np.stack([x.mask for x in layers]).sum(axis=0), visible.astype(int))
    return layers


def write_semantic_manifest(source: Path, output: Path, layers: Sequence[Candidate],
                            backend: str, prompts: Sequence[str]) -> Path:
    """Write hash-checked, independently addressable raw 0/255 masks."""
    source = Path(source)
    output = Path(output)
    if not source.is_file() or source.stat().st_size > MAX_SOURCE_BYTES:
        raise ValueError("source image missing or over 32MiB")
    with Image.open(source) as img:
        img.verify()
    with Image.open(source) as img:
        oriented = ImageOps.exif_transpose(img)
        width, height = oriented.size
        visible = (np.asarray(oriented.getchannel("A")) > 0 if "A" in oriented.getbands()
                   else np.ones((height, width), dtype=bool))
    if backend != "sam3-image":
        raise ValueError("unrecognized model backend")
    if not layers or len(layers) > MAX_LAYERS or width * height > MAX_PIXELS:
        raise ValueError("empty or oversized layer document")
    ext = source.suffix.lower()
    if ext not in (".png", ".jpg", ".jpeg"):
        raise ValueError("native pipeline accepts PNG or JPEG source")
    assigned = np.zeros((height, width), dtype=bool)
    for layer in layers:
        if np.asarray(layer.mask).shape != (height, width):
            raise ValueError("layer/source dimension mismatch")
        if not layer.name or len(layer.name) > 128 or not math.isfinite(layer.score) or not 0 <= layer.score <= 1:
            raise ValueError("invalid semantic layer fields")
        arr = np.asarray(layer.mask, dtype=bool)
        if not arr.any():
            raise ValueError("empty semantic layer")
        if np.any(assigned & arr):
            raise ValueError("layer masks overlap")
        assigned |= arr
    if not np.array_equal(assigned, visible):
        raise ValueError("semantic layers must exactly cover visible pixels")
    if output.exists() and any(output.iterdir()):
        raise ValueError("output directory must be empty")
    output.mkdir(parents=True, exist_ok=True)
    (output / "masks").mkdir(exist_ok=True)
    image_name = f"source{ext}"
    shutil.copyfile(source, output / image_name)
    serialized = []
    for index, layer in enumerate(layers):
        arr = np.asarray(layer.mask, dtype=bool)
        content = np.where(arr, 255, 0).astype(np.uint8).tobytes()
        filename = f"masks/layer-{index:02d}.u8"
        (output / filename).write_bytes(content)
        serialized.append({"name":layer.name,"score":float(layer.score),"mask":filename,
                           "sha256":hashlib.sha256(content).hexdigest(),
                           "pixels":int(arr.sum()),"isBackground":bool(layer.is_background)})
    src_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    result = {"schema":1,"backend":backend,"image":image_name,"sourceSha256":src_hash,
              "width":width,"height":height,"promptOrder":"back-to-front",
              "prompts":list(prompts),"objects":serialized,
              "containsInpaintedOcclusion":False,"automaticSemanticAccuracyVerified":False}
    manifest = output / "manifest.json"
    manifest.write_text(json.dumps(result,ensure_ascii=False,sort_keys=True,indent=2)+"\n",encoding="utf-8")
    return manifest


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run official Meta SAM 3 on named objects; emit Drawborad native review layers")
    parser.add_argument("--input",type=Path,required=True,help="PNG/JPEG image")
    parser.add_argument("--output",type=Path,required=True,help="new/empty directory")
    parser.add_argument("--prompt",action="append",required=True,help="short noun phrase, in BACK-to-FRONT order; repeat")
    parser.add_argument("--checkpoint",type=Path,default=None,help="local SAM 3 IMAGE checkpoint (SAM 3.1 multiplex video is different)")
    parser.add_argument("--device",choices=["cpu","cuda"],default="cuda")
    parser.add_argument("--min-score",type=float,default=0.5)
    args = parser.parse_args(argv)
    if len(args.prompt) > MAX_LAYERS-1 or not 0 <= args.min_score <= 1:
        parser.error("invalid prompt count or confidence")
    if args.checkpoint is None:
        parser.error("pass --checkpoint for a locally installed official SAM 3 image checkpoint; no hidden download")
    if not args.checkpoint.is_file():
        parser.error("SAM 3 image checkpoint not found")
    with Image.open(args.input) as source:
        if source.width*source.height>MAX_PIXELS:
            parser.error("maximum 4 million source pixels")
        image = ImageOps.exif_transpose(source).convert("RGB")
        alpha = None
        if "A" in source.getbands():
            alpha = np.asarray(ImageOps.exif_transpose(source).getchannel("A"))
    # Imported only after validating arguments so test environments need not
    # install a 848M-parameter model or GPU just to validate the file format.
    from sam3.model_builder import build_sam3_image_model
    from sam3.model.sam3_image_processor import Sam3Processor
    import torch
    if args.device=="cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA selected but no CUDA device is available")
    model = build_sam3_image_model(checkpoint_path=str(args.checkpoint),load_from_HF=False,device=args.device)
    processor = Sam3Processor(model,device=args.device,confidence_threshold=args.min_score)
    candidates = infer_candidates(processor,image,args.prompt,args.min_score)
    if not candidates:
        raise RuntimeError("SAM 3 detected no matching named objects: no silent fake masks")
    layers = build_semantic_partition(candidates,(image.height,image.width),alpha)
    manifest = write_semantic_manifest(args.input,args.output,layers,"sam3-image",args.prompt)
    print(manifest)
    return 0


if __name__=="__main__":
    raise SystemExit(main())