#!/usr/bin/env python3
"""Linux laboratory proof: prompted segmentation -> editable stroke events -> source-free replay.

This is NOT the native Drawborad engine, a SAM model, a semantic detector, or
commercial quality reconstruction. It is an executable cross-platform contract
probe for the fundamental workflow, with deliberately explicit provenance.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
from collections import OrderedDict

import cv2
import numpy as np
from PIL import Image


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def checked_image(image: np.ndarray) -> tuple[int, int]:
    if image.ndim != 3 or image.shape[2] != 3 or image.dtype != np.uint8:
        raise ValueError("expected uint8 RGB image")
    h, w = image.shape[:2]
    if w < 4 or h < 4 or w > 4096 or h > 4096 or w * h > 4_000_000:
        raise ValueError("image dimensions are outside the lab limits")
    return w, h


def extract_prompt_region(image: np.ndarray, rect: tuple[int, int, int, int], iterations: int = 5) -> np.ndarray:
    """Segment a prompted region using OpenCV GrabCut; labels are NOT auto-inferred."""
    w, h = checked_image(image)
    x, y, rw, rh = rect
    if x < 0 or y < 0 or rw < 3 or rh < 3 or x + rw > w or y + rh > h:
        raise ValueError("prompt rectangle out of bounds")
    if x == 0 and y == 0 and rw == w and rh == h:
        raise ValueError("grabcut rectangle must leave some background")
    bgr = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)
    flags = np.zeros((h, w), np.uint8)
    background_model = np.zeros((1, 65), np.float64)
    foreground_model = np.zeros((1, 65), np.float64)
    cv2.grabCut(bgr, flags, (int(x), int(y), int(rw), int(rh)),
                background_model, foreground_model, int(iterations), cv2.GC_INIT_WITH_RECT)
    binary = np.where((flags == cv2.GC_FGD) | (flags == cv2.GC_PR_FGD), 255, 0).astype(np.uint8)
    if not np.any(binary):
        raise ValueError("prompt yielded an empty region; adjust rectangle")
    return binary


def build_partition(regions: list[tuple[str, np.ndarray]], width: int, height: int,
                    *, resolve_overlap: bool = True, include_background: bool = True) -> dict[str, np.ndarray]:
    """Higher priority region first; reject or trim overlap, preserve all pixels."""
    assigned = np.zeros((height, width), bool)
    out: dict[str, np.ndarray] = OrderedDict()
    for name, raw in regions:
        if not isinstance(name, str) or not name or name in out or name == "background":
            raise ValueError("region names must be unique nonempty strings except background")
        if raw.shape != (height, width) or raw.dtype != np.uint8 or not np.isin(raw, [0, 255]).all():
            raise ValueError("region masks must be full-sized binary uint8")
        take = raw != 0
        if not resolve_overlap and np.any(take & assigned):
            raise ValueError("overlap between regions")
        take &= ~assigned
        assigned |= take
        if take.any():
            out[name] = np.where(take, 255, 0).astype(np.uint8)
    if include_background:
        out["background"] = np.where(~assigned, 255, 0).astype(np.uint8)
        assigned[:] = True
    if not assigned.all():
        raise ValueError("unassigned pixels remain in the partition")
    return out


def plan_events(image: np.ndarray, masks: dict[str, np.ndarray], *, coarse: int = 16,
                detail: int = 6) -> list[dict]:
    """Plan bounded horizontal painted brush marks from visible RGB, never bitmap events."""
    w, h = checked_image(image)
    if not 1 <= detail <= coarse <= 64:
        raise ValueError("invalid brush stage sizes")
    if sum(np.count_nonzero(m) for m in masks.values()) != w * h:
        raise ValueError("masks must cover the image exactly once")
    coverage = np.zeros((h, w), np.uint8)
    for mask in masks.values():
        if mask.shape != (h, w) or mask.dtype != np.uint8 or np.any(coverage & (mask != 0)):
            raise ValueError("mask geometry or overlap invalid")
        coverage |= mask != 0
    events = []
    for stage, block in (("base", coarse), ("detail", detail)):
        for name, mask in masks.items():
            for y0 in range(0, h, block):
                y1 = min(y0 + block, h)
                for x0 in range(0, w, block):
                    x1 = min(x0 + block, w)
                    local = mask[y0:y1, x0:x1] > 0
                    if not local.any():
                        continue
                    color = np.rint(np.mean(image[y0:y1, x0:x1][local], axis=0)).astype(np.uint8)
                    # Stable geometry, color, and order. No original-pixel payload.
                    events.append({"id": len(events) + 1, "object": name, "stage": stage,
                                   "color": [int(v) for v in color],
                                   "x0": x0, "x1": x1 - 1, "y": min(h - 1, y0 + (y1-y0-1)//2),
                                   "width": block})
    return events


def render_events(events: list[dict], masks: dict[str, np.ndarray], width: int, height: int) -> np.ndarray:
    """Render from *only* events and reviewed masks. Source image is not an argument."""
    if width <= 0 or height <= 0:
        raise ValueError("invalid canvas")
    layers = {name: np.zeros((height, width, 4), np.uint8) for name in masks}
    for event in events:
        name = event['object']
        if name not in layers:
            raise ValueError("event targets unknown object")
        x0, x1, y, size = (event[k] for k in ('x0', 'x1', 'y', 'width'))
        if not (isinstance(x0, int) and isinstance(x1, int) and isinstance(y, int) and
                isinstance(size, int) and 0 <= x0 <= x1 < width and 0 <= y < height and 1 <= size <= 64):
            raise ValueError("invalid stroke geometry")
        color = event['color']
        if len(color) != 3 or any(not isinstance(c, int) or c < 0 or c > 255 for c in color):
            raise ValueError("invalid stroke color")
        # OpenCV draws into RGBA. Every later mask operation remains independent.
        cv2.line(layers[name], (x0, y), (x1, y), (*color, 255), size, lineType=cv2.LINE_8)
    result = np.zeros((height, width, 4), np.uint8)
    for name, mask in masks.items():
        if mask.shape != (height, width):
            raise ValueError("mask geometry invalid")
        visible = mask > 0
        result[visible] = layers[name][visible]
    return result


def save_project(directory: pathlib.Path, masks: dict[str, np.ndarray], events: list[dict],
                 width: int, height: int) -> None:
    directory = pathlib.Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    rows = []
    for index, (name, mask) in enumerate(masks.items()):
        if mask.shape != (height, width) or mask.dtype != np.uint8:
            raise ValueError("cannot persist an incompatible mask")
        filename = f"mask-{index:02}.u8"
        data = mask.tobytes()
        (directory / filename).write_bytes(data)
        rows.append({"name": name, "path": filename, "sha256": digest(data),
                     "pixels": int(np.count_nonzero(mask))})
    payload = json.dumps(events, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()
    (directory / "events.json").write_bytes(payload)
    manifest = {"schema": 1, "width": width, "height": height,
                "renderer": "lab-cv2-horizontal-strokes-v1",
                "sourceIncluded": False, "modelInferredSemanticNames": False,
                "masks": rows, "eventCount": len(events), "eventSha256": digest(payload)}
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")


def load_project(directory: pathlib.Path) -> tuple[dict[str, np.ndarray], list[dict], int, int]:
    directory = pathlib.Path(directory)
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("sourceIncluded") is not False:
        raise ValueError("unsupported project manifest")
    w, h = manifest['width'], manifest['height']
    if not isinstance(w, int) or not isinstance(h, int) or w < 1 or h < 1 or w * h > 4_000_000:
        raise ValueError("invalid project dimensions")
    masks = OrderedDict()
    for item in manifest['masks']:
        filename = item['path']
        if pathlib.PurePath(filename).name != filename or not filename.startswith('mask-'):
            raise ValueError("unsafe mask path")
        raw = (directory / filename).read_bytes()
        if len(raw) != w * h or digest(raw) != item['sha256']:
            raise ValueError("mask hash or dimensions invalid")
        masks[item['name']] = np.frombuffer(raw, np.uint8).reshape(h, w).copy()
    if np.any(np.sum(np.stack([(m > 0) for m in masks.values()], axis=0), axis=0) != 1):
        raise ValueError("project masks must partition image")
    raw_events = (directory / 'events.json').read_bytes()
    if digest(raw_events) != manifest['eventSha256']:
        raise ValueError("stroke events integrity mismatch")
    events = json.loads(raw_events)
    if len(events) != manifest['eventCount'] or len({e['id'] for e in events}) != len(events):
        raise ValueError("invalid stroke event identity")
    return masks, events, w, h


def make_overlay(image: np.ndarray, masks: dict[str, np.ndarray]) -> np.ndarray:
    palette = np.array([[8, 176, 246], [250, 100, 95], [246, 190, 40], [150, 109, 223],
                        [94, 201, 140], [239, 80, 177]], np.uint8)
    tint = image.copy()
    for i, (_, mask) in enumerate(masks.items()):
        if mask.any():
            selected = mask > 0
            tint[selected] = np.clip(image[selected].astype(np.float32)*0.64 +
                                    palette[i % len(palette)]*0.36, 0, 255).astype(np.uint8)
    return tint


def background_on_white(rgba: np.ndarray) -> np.ndarray:
    return np.where(rgba[:, :, 3:4] > 0, rgba[:, :, :3], 255).astype(np.uint8)


def parse_rect(value: str) -> tuple[str, tuple[int, int, int, int]]:
    try:
        name, rectangle = value.split(':', 1)
        vals = tuple(map(int, rectangle.split(',')))
    except (ValueError, TypeError) as exc:
        raise argparse.ArgumentTypeError("rectangle must be name:x,y,width,height") from exc
    if not name or name == "background" or len(vals) != 4:
        raise argparse.ArgumentTypeError("rectangle must be name:x,y,width,height")
    return name, vals


def run_cli(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input', required=True, type=pathlib.Path)
    p.add_argument('--out', required=True, type=pathlib.Path)
    p.add_argument('--rect', action='append', default=[], type=parse_rect,
                   help='Human-provided ROI; first rectangles have foreground priority')
    p.add_argument('--coarse', type=int, default=16)
    p.add_argument('--detail', type=int, default=6)
    args = p.parse_args(argv)
    if not args.rect:
        p.error('at least one --rect name:x,y,width,height required (no fake auto naming)')
    image_file_bytes = args.input.read_bytes()
    image = np.asarray(Image.open(args.input).convert('RGB'))
    w, h = checked_image(image)
    regions = []
    for name, rect in args.rect:
        region = extract_prompt_region(image, rect)
        regions.append((name, region))
    masks = build_partition(regions, w, h)
    events = plan_events(image, masks, coarse=args.coarse, detail=args.detail)
    save_project(args.out / 'project', masks, events, w, h)
    # Prove reading saved events and masks is enough for all subsequent frames.
    saved_masks, saved_events, sw, sh = load_project(args.out / 'project')
    previews = args.out / 'previews'
    previews.mkdir(parents=True, exist_ok=True)
    Image.fromarray(make_overlay(image, saved_masks)).save(previews / 'mask-overlay.png')
    for i, (name, mask) in enumerate(saved_masks.items()):
        rgba = np.dstack([image, mask])
        rgba[mask == 0] = 0
        Image.fromarray(rgba, 'RGBA').save(previews / f'layer-{i:02}.png')
    for fraction in (0, .25, .5, .75, 1.0):
        frame = render_events(saved_events[:int(len(saved_events)*fraction)], saved_masks, sw, sh)
        Image.fromarray(background_on_white(frame), 'RGB').save(previews / f'replay-{int(fraction*100):03}.png')
    final = render_events(saved_events, saved_masks, sw, sh)
    visible = background_on_white(final)
    from skimage.metrics import structural_similarity as ssim
    report = {"status": "LAB_PROBE_NOT_NATIVE", "semanticRecognition": "not-tested",
              "segmentationEngine": "cv2.grabCut prompted rectangles, not SAM",
              "modelWeightsAvailable": False, "nativeDrawboradExecution": False,
              "sourceSha256": digest(image_file_bytes), "shape": [h, w],
              "objectCountIncludingBackground": len(saved_masks),
              "events": len(saved_events), "sourceAssetReadsDuringReplay": 0,
              "coveredPixels": int(np.count_nonzero(final[:, :, 3])),
              "totalPixels": int(w*h),
              "rgbMae": float(np.mean(np.abs(image.astype(np.float64) - visible.astype(np.float64)))/255.0),
              "ssimRgb": float(ssim(image, visible, channel_axis=2, data_range=255)),
              "fidelityPassed": False, "naturalnessPassed": False,
              "masks": {n: int(np.count_nonzero(m)) for n, m in saved_masks.items()}}
    (args.out / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(run_cli())