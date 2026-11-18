#!/usr/bin/env python3
"""Integration test for --planner artist, Amodal background, compile-plan, and 3300x5100 300 DPI super-res."""
from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

import numpy as np
from PIL import Image, ImageDraw

engine = str(Path(sys.argv[1]).resolve())


def run_cli(*args: str, timeout: int = 180) -> subprocess.CompletedProcess[str]:
    return subprocess.run([engine, *args], text=True, capture_output=True, timeout=timeout)


with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)

    # 1. Build a 128x160 two-object scene (background gradient + foreground circle + dark ink line)
    bundle = root / 'scene'
    (bundle / 'masks').mkdir(parents=True)
    w, h = 128, 160
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rgb = np.zeros((h, w, 3), dtype=np.uint8)
    rgb[:, :, 0] = np.clip(45 + 150 * (yy / h), 0, 255).astype(np.uint8)
    rgb[:, :, 1] = np.clip(90 + 80 * (xx / w), 0, 255).astype(np.uint8)
    rgb[:, :, 2] = 175
    im = Image.fromarray(rgb, mode='RGB')
    draw = ImageDraw.Draw(im)
    draw.ellipse((34, 42, 94, 112), fill=(235, 175, 145), outline=(22, 18, 28), width=3)
    draw.ellipse((54, 62, 64, 72), fill=(255, 255, 255))
    src_path = bundle / 'source.png'
    im.save(src_path)

    fg_mask = (((xx - 64.0) / 31.0) ** 2 + ((yy - 77.0) / 36.0) ** 2 <= 1.0).astype(np.uint8) * 255
    bg_mask = (fg_mask == 0).astype(np.uint8) * 255
    objects = []
    for i, (name, m, bg_flag) in enumerate([('Sky backdrop', bg_mask, True), ('Foreground character', fg_mask, False)]):
        raw = m.tobytes()
        rel = f'masks/layer-{i:02d}.u8'
        (bundle / rel).write_bytes(raw)
        objects.append({
            'name': name,
            'score': 0.99,
            'isBackground': bg_flag,
            'pixels': int(np.count_nonzero(m)),
            'mask': rel,
            'sha256': hashlib.sha256(raw).hexdigest(),
        })
    manifest = bundle / 'manifest.json'
    manifest.write_text(json.dumps({
        'schema': 1,
        'backend': 'sam3-image',
        'promptOrder': 'back-to-front',
        'amodalBackground': True,
        'image': 'source.png',
        'sourceSha256': hashlib.sha256(src_path.read_bytes()).hexdigest(),
        'width': w,
        'height': h,
        'objects': objects,
    }))

    # Run --planner artist and verify Amodal background has ZERO foreground hole + zero Guides layer!
    artist_out = root / 'artist_native'
    res = run_cli('run', '--manifest', str(manifest), '--output', str(artist_out), '--accept-all', '--planner', 'artist')
    assert res.returncode == 0, res.stderr
    rep = json.loads(res.stdout)
    assert rep['planner'] == 'artist'
    assert rep['amodalBackground'] is True
    assert rep['replaySourceAssetReads'] == 0
    assert rep['reopenReplayMatches'] is True
    assert rep['strokeEvents'] > 0

    with zipfile.ZipFile(artist_out / 'painted.ora') as zf:
        stack = ET.fromstring(zf.read('stack.xml'))
        layers = list(stack.find('stack'))
        names = [l.attrib['name'] for l in layers]
        assert not any('Guides' in n for n in names), f'Unexpected gray guides layer in {names}'
        ops = {l.attrib['composite-op'] for l in layers}
        assert 'svg:src-over' in ops
        # Bottom-most layer in ORA stack is background's Flats · Silhouette -> check occluded center (64, 77) is painted!
        bg_flats_entry = layers[-1]
        bg_flats_rgba = np.asarray(Image.open(io.BytesIO(zf.read(bg_flats_entry.attrib['src']))).convert('RGBA'))
        assert bg_flats_rgba[77, 64, 3] == 255, 'Amodal background Flats layer must have no cutout hole behind foreground'

    # 1b. Verify amodalBackground=False (without --amodal-background) stays opt-in even when isBackground=True!
    manifest.write_text(json.dumps({
        'schema': 1,
        'backend': 'sam3-image',
        'promptOrder': 'back-to-front',
        'amodalBackground': False,
        'image': 'source.png',
        'sourceSha256': hashlib.sha256(src_path.read_bytes()).hexdigest(),
        'width': w,
        'height': h,
        'objects': objects,
    }))
    non_amodal_out = root / 'artist_non_amodal'
    res_na = run_cli('run', '--manifest', str(manifest), '--output', str(non_amodal_out), '--accept-all', '--planner', 'artist')
    assert res_na.returncode == 0, res_na.stderr
    assert json.loads(res_na.stdout)['amodalBackground'] is False
    with zipfile.ZipFile(non_amodal_out / 'painted.ora') as zf:
        stack_na = ET.fromstring(zf.read('stack.xml'))
        bg_na_entry = list(stack_na.find('stack'))[-1]
        bg_na_rgba = np.asarray(Image.open(io.BytesIO(zf.read(bg_na_entry.attrib['src']))).convert('RGBA'))
        assert bg_na_rgba[77, 64, 3] == 0, 'Non-amodal background must preserve disjoint foreground cutout'

    # 2. Verify compile-plan + 3300x5100 (300 DPI) native continuous super-resolution!
    plan_file = root / 'plan.json'
    plan_file.write_text(json.dumps({
        'schema': 'drawborad.stroke-plan.v1',
        'planner': 'test-artist-plan',
        'width': 1024,
        'height': 1536,
        'dpi': 300,
        'objects': [
            {
                'name': 'Poster Hero',
                'layers': [
                    {
                        'name': 'Flats · Silhouette',
                        'stage': 'artist.flats',
                        'blend': 'normal',
                        'opacity': 1.0,
                        'strokes': [{
                            'engine': 8,
                            'tool': 'bucket',
                            'color': [0.18, 0.24, 0.42, 1.0],
                            'polygon': [[120.0, 180.0], [900.0, 200.0], [860.0, 1320.0], [150.0, 1280.0]],
                        }],
                    },
                    {
                        'name': 'Shadow Glaze',
                        'stage': 'artist.form_glaze',
                        'blend': 'multiply',
                        'opacity': 0.85,
                        'strokes': [{
                            'engine': 2,
                            'tool': 'marker',
                            'width': 24.0,
                            'opacity': 0.5,
                            'hardness': 0.25,
                            'color': [0.45, 0.40, 0.65, 1.0],
                            'points': [[200.0, 300.0, 0.4], [512.0, 768.0, 0.95], [820.0, 1180.0, 0.35]],
                        }],
                    },
                    {
                        'name': 'Hero Ink & Dry Tooth',
                        'stage': 'artist.hero_ink',
                        'blend': 'normal',
                        'opacity': 1.0,
                        'strokes': [
                            {
                                'engine': 12,
                                'tool': 'dry-ink',
                                'width': 8.0,
                                'opacity': 0.92,
                                'color': [0.06, 0.05, 0.08, 1.0],
                                'seed': 77777,
                                'physicalDryInk': True,
                                'points': [[180.0, 260.0, 0.3], [520.0, 540.0, 0.95], [840.0, 890.0, 0.2]],
                            },
                            {
                                'engine': 21,
                                'tool': 'chisel-ink-aa',
                                'width': 4.5,
                                'opacity': 0.95,
                                'angle': 2,
                                'color': [0.08, 0.08, 0.10, 1.0],
                                'points': [[260.0, 400.0, 0.8], [740.0, 420.0, 0.9]],
                            },
                        ],
                    },
                    {
                        'name': 'Highlights',
                        'stage': 'artist.highlights',
                        'blend': 'screen',
                        'opacity': 0.9,
                        'strokes': [{
                            'engine': 19,
                            'tool': 'smooth-ink-aa',
                            'width': 3.2,
                            'opacity': 0.85,
                            'color': [0.85, 0.90, 1.0, 1.0],
                            'points': [[300.0, 350.0, 0.3], [500.0, 360.0, 1.0], [700.0, 380.0, 0.3]],
                        }],
                    },
                ],
            }
        ],
    }))

    sr_out = root / 'compiled_3300x5100'
    sr_res = run_cli('compile-plan', '--plan', str(plan_file), '--output', str(sr_out),
                     '--target-size', '3300x5100', '--target-dpi', '300', timeout=240)
    assert sr_res.returncode == 0, sr_res.stderr
    sr_rep = json.loads(sr_res.stdout)
    assert sr_rep['width'] == 3300 and sr_rep['height'] == 5100
    assert sr_rep['dpi'] == 300
    assert sr_rep['replaySourceAssetReads'] == 0
    assert sr_rep['reopenReplayMatches'] is True
    with Image.open(sr_out / 'final.png') as final_im:
        assert final_im.size == (3300, 5100)

    # 3. Stress 50 artist layers (10 objects x 5 stages) + concave self-intersecting polygons
    #    at 3300x5100 AND extreme 8192x2440 (~19.99M px) aspect ratio + cross-canvas engine2 pencil!
    objs_50 = []
    for oi in range(10):
        y_off = 10 + oi * 100
        objs_50.append({
            'name': f'Object_{oi}',
            'layers': [
                {
                    'name': 'Flats',
                    'stage': 'artist.flats',
                    'blend': 'normal',
                    'strokes': [{
                        'engine': 8,
                        'tool': 'bucket',
                        'color': [0.2, 0.3, 0.45, 1.0],
                        # Bowtie / concave polygon including horizontal edge (yi == yj)
                        'polygon': [[15.0, float(y_off)], [880.0, float(y_off)],
                                    [440.0, float(y_off + 45)], [880.0, float(y_off + 85)],
                                    [15.0, float(y_off + 85)]],
                    }],
                },
                {
                    'name': 'Glaze',
                    'stage': 'artist.form_glaze',
                    'blend': 'multiply',
                    'strokes': [{
                        'engine': 2,
                        'tool': 'pencil',
                        'width': 22.0,
                        'opacity': 0.5,
                        'hardness': 0.25,
                        'color': [0.4, 0.4, 0.6, 1.0],
                        'points': [[40.0, float(y_off + 25), 0.5], [840.0, float(y_off + 65), 0.9]],
                    } for _ in range(4)],
                },
                {
                    'name': 'Structure',
                    'stage': 'artist.structure',
                    'blend': 'normal',
                    'strokes': [{
                        'engine': 19,
                        'tool': 'smooth-ink-aa',
                        'width': 3.5,
                        'opacity': 0.9,
                        'color': [0.5, 0.6, 0.7, 1.0],
                        'points': [[50.0, float(y_off + 30), 0.4], [830.0, float(y_off + 60), 0.9]],
                    }],
                },
                {
                    'name': 'Ink',
                    'stage': 'artist.hero_ink',
                    'blend': 'normal',
                    'strokes': [{
                        'engine': 12,
                        'tool': 'dry-ink',
                        'width': 4.0,
                        'opacity': 0.92,
                        'seed': 100 + oi,
                        'physicalDryInk': True,
                        'color': [0.08, 0.08, 0.1, 1.0],
                        'points': [[55.0, float(y_off + 35), 0.5], [825.0, float(y_off + 55), 0.85]],
                    }],
                },
                {
                    'name': 'Highlights',
                    'stage': 'artist.highlights',
                    'blend': 'screen',
                    'strokes': [{
                        'engine': 19,
                        'tool': 'smooth-ink-aa',
                        'width': 2.0,
                        'opacity': 0.8,
                        'color': [0.9, 0.95, 1.0, 1.0],
                        'points': [[60.0, float(y_off + 20), 0.3], [820.0, float(y_off + 40), 0.9]],
                    }],
                },
            ],
        })
    plan_50 = root / 'plan_50.json'
    plan_50.write_text(json.dumps({
        'schema': 'drawborad.stroke-plan.v1',
        'width': 1024,
        'height': 1536,
        'objects': objs_50,
    }))
    r50 = run_cli('compile-plan', '--plan', str(plan_50), '--output', str(root / 'out_50_3300'),
                  '--target-size', '3300x5100', timeout=240)
    assert r50.returncode == 0, r50.stderr
    assert json.loads(r50.stdout)['paintLayers'] == 50

    r_aspect = run_cli('compile-plan', '--plan', str(plan_50), '--output', str(root / 'out_50_aspect'),
                       '--target-size', '8192x2440', timeout=240)
    assert r_aspect.returncode == 0, r_aspect.stderr

    # 4. Edge-case negative checks (size limits, bad schema, non-numeric / out-of-range coordinates)
    assert run_cli('compile-plan', '--plan', str(plan_file), '--output', str(root / 'bad_sz'),
                   '--target-size', '9999x9999').returncode == 2
    bad_plan = root / 'bad_plan.json'
    bad_plan.write_text(json.dumps({'schema': 'wrong', 'width': 64, 'height': 64, 'objects': []}))
    assert run_cli('compile-plan', '--plan', str(bad_plan), '--output', str(root / 'bad_out')).returncode == 3

    bad_poly = root / 'bad_poly.json'
    bad_poly.write_text(json.dumps({
        'schema': 'drawborad.stroke-plan.v1',
        'width': 64,
        'height': 64,
        'objects': [{'name': 'O', 'layers': [{'name': 'L', 'stage': 'artist.flats', 'strokes': [{
            'engine': 8, 'color': [0.5, 0.5, 0.5, 1.0],
            'polygon': [['not-a-number', 10.0], [20.0, 10.0], [20.0, 20.0]],
        }]}]}],
    }))
    assert run_cli('compile-plan', '--plan', str(bad_poly), '--output', str(root / 'bad_poly_out')).returncode == 3

    for idx_bad, bad_stroke in enumerate([
        {'engine': 8, 'color': ['bad', 0.5, 0.5, 1.0], 'spans': [[0, 10, 5]]},
        {'engine': 8, 'color': [0.5, 0.5, 0.5, 1.0], 'spans': [[1e50, 10, 5]]},
        {'engine': 8, 'color': [0.5, 0.5, 0.5, 1.0], 'spans': [[12, 4, 5]]},
        {'engine': 19, 'color': [0.5, 0.5, 0.5, 1.0], 'width': 'wide', 'points': [[5.0, 5.0], [20.0, 20.0]]},
    ]):
        bp = root / f'bad_num_{idx_bad}.json'
        bp.write_text(json.dumps({
            'schema': 'drawborad.stroke-plan.v1',
            'width': 64,
            'height': 64,
            'objects': [{'name': 'O', 'layers': [{'name': 'L', 'stage': 'artist.flats', 'strokes': [bad_stroke]}]}],
        }))
        assert run_cli('compile-plan', '--plan', str(bp), '--output', str(root / f'bad_num_out_{idx_bad}')).returncode == 3

print('PASS artist planner, amodal background, compile-plan, and 3300x5100 300 DPI super-resolution')
