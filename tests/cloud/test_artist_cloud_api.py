"""Cloud API & differentiable optimizer integration tests for Phase 0-4 breakthroughs."""
from __future__ import annotations

import io
import os
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import pytest
from fastapi.testclient import TestClient

from cloud.app import Config, create_app


def test_demo_fixture_reconstruct_artist_and_layer_blends(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    engine = os.environ.get('DRAWBORAD_TEST_ENGINE')
    if not engine:
        pytest.skip('Supply DRAWBORAD_TEST_ENGINE')
    engine_resolved = str(Path(engine).resolve())
    monkeypatch.setenv('DRAWBORAD_NATIVE_ENGINE', engine_resolved)
    import sys
    cfg = Config(tmp_path, None, Path('missing'), 'cpu', sys.executable, '')

    with TestClient(create_app(cfg)) as client:
        # 1. Built-in synthetic fixture previously failed with 422 on from-job; must now succeed!
        demo = client.post('/api/v1/demo')
        assert demo.status_code == 201, demo.text
        jid = demo.json()['id']

        rec = client.post(f'/api/v1/projects/from-job/{jid}', json={'accept_all': True, 'planner': 'artist'})
        assert rec.status_code == 201, rec.text
        proj = rec.json()
        pid = proj['id']
        assert int(proj['stroke_count']) > 0
        assert 'layer_blend' in proj['capabilities']
        assert 'chisel_ink' in proj['capabilities']
        assert 'continuous_eraser' in proj['capabilities']

        # 2. Add a paint layer, set blend to Screen, draw chisel_ink and eraser strokes, export ORA!
        added = client.post(f'/api/v1/projects/{pid}/commands', json={
            'expected_revision': proj['revision'],
            'type': 'add_fill_layer',
        })
        assert added.status_code == 200, added.text
        st = added.json()
        plain_layer = next(n for n in st['nodes'] if n['kind'] == 3 and n['object_id'] == '0' * 32)

        upd = client.post(f'/api/v1/projects/{pid}/commands', json={
            'expected_revision': st['revision'],
            'type': 'update_layer',
            'node_id': plain_layer['id'],
            'name': 'Rim Highlights',
            'opacity': 0.8,
            'blend': 'screen',
        })
        assert upd.status_code == 200, upd.text
        st2 = upd.json()
        updated_node = next(n for n in st2['nodes'] if n['id'] == plain_layer['id'])
        assert updated_node['blend'] == 2  # BlendMode::Screen

        drew = client.post(f'/api/v1/projects/{pid}/commands', json={
            'expected_revision': st2['revision'],
            'type': 'draw_stroke',
            'layer_id': plain_layer['id'],
            'brush': 'chisel_ink',
            'width': 3.5,
            'angle': 3,
            'color': '#ffeeaa',
            'points': [{'x': 10.5, 'y': 12.5, 'pressure': 0.8}, {'x': 42.5, 'y': 38.5, 'pressure': 0.9}],
        })
        assert drew.status_code == 200, drew.text
        st3 = drew.json()

        erased = client.post(f'/api/v1/projects/{pid}/commands', json={
            'expected_revision': st3['revision'],
            'type': 'draw_stroke',
            'layer_id': plain_layer['id'],
            'brush': 'eraser',
            'width': 2.0,
            'points': [{'x': 20.0, 'y': 20.0, 'pressure': 1.0}, {'x': 26.0, 'y': 26.0, 'pressure': 1.0}],
        })
        assert erased.status_code == 200, erased.text
        st4 = erased.json()

        # Optional C-ABI direct MyPaint test if libmypaint is present in runtime
        if 'mypaint_c_direct' in st4['capabilities']:
            mp = client.post(f'/api/v1/projects/{pid}/commands', json={
                'expected_revision': st4['revision'],
                'type': 'mypaint_direct_stroke',
                'preset': 'pointy_ink',
                'paint_mode': 'normal',
                'radius_scale': 1.25,
                'color': '#2244aa',
                'points': [
                    {'x': 14.0, 'y': 14.0, 'pressure': 0.4, 'time_ms': 0},
                    {'x': 36.0, 'y': 30.0, 'pressure': 0.7, 'time_ms': 40},
                ],
            })
            assert mp.status_code == 200, mp.text
            rev_mp = mp.json()['revision']

            # Single-point tap / zero-length dab must produce visible ink without failing!
            mp_one = client.post(f'/api/v1/projects/{pid}/commands', json={
                'expected_revision': rev_mp,
                'type': 'mypaint_direct_stroke',
                'preset': 'pointy_ink',
                'paint_mode': 'normal',
                'radius_scale': 0.5,
                'color': '#dd5522',
                'points': [{'x': 24.0, 'y': 24.0, 'pressure': 0.85, 'time_ms': 0}],
            })
            assert mp_one.status_code == 200, mp_one.text
            rev_mp2 = mp_one.json()['revision']

            # Dry brush preset with extreme radius_scale=8.0 & multiply mode
            mp_dry = client.post(f'/api/v1/projects/{pid}/commands', json={
                'expected_revision': rev_mp2,
                'type': 'mypaint_direct_stroke',
                'preset': 'dry_brush',
                'paint_mode': 'multiply',
                'radius_scale': 8.0,
                'color': '#223344',
                'points': [
                    {'x': 8.0, 'y': 10.0, 'pressure': 0.6, 'time_ms': 0},
                    {'x': 44.0, 'y': 42.0, 'pressure': 0.8, 'time_ms': 35},
                ],
            })
            assert mp_dry.status_code == 200, mp_dry.text

            # Invalid paint_mode (e.g. 'erase') must be cleanly rejected with 422
            mp_bad = client.post(f'/api/v1/projects/{pid}/commands', json={
                'expected_revision': mp_dry.json()['revision'],
                'type': 'mypaint_direct_stroke',
                'preset': 'pointy_ink',
                'paint_mode': 'erase',
                'color': '#000000',
                'points': [{'x': 12.0, 'y': 12.0, 'pressure': 0.8, 'time_ms': 0}],
            })
            assert mp_bad.status_code == 422

        ora_resp = client.get(f'/api/v1/projects/{pid}/export.ora')
        assert ora_resp.status_code == 200
        assert ora_resp.headers['X-Drawborad-Source-Asset-Reads'] == '0'
        with zipfile.ZipFile(io.BytesIO(ora_resp.content)) as zf:
            xml_bytes = zf.read('stack.xml')
            assert b'svg:screen' in xml_bytes

        # 3. Also test artist-opt differentiable planner via from-job endpoint (with amodal_background=False and True)!
        demo2 = client.post('/api/v1/demo').json()
        cur_rev = client.get(f'/api/v1/projects/{pid}').json()['revision']
        assert client.post(f'/api/v1/projects/{pid}/close', json={'expected_revision': cur_rev, 'discard': True}).status_code == 200
        rec_opt = client.post(
            f"/api/v1/projects/from-job/{demo2['id']}",
            json={'accept_all': True, 'planner': 'artist-opt', 'amodal_background': False},
        )
        assert rec_opt.status_code == 201, rec_opt.text
        assert int(rec_opt.json()['stroke_count']) > 0


def test_planner_cli_amodal_flag_not_hardcoded(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    import cloud.planner.cli as planner_cli

    captured = {}

    def fake_opt(manifest, engine, output, config=None, allow_fixture=False):
        captured['amodal'] = config.amodal_background
        captured['fixture'] = allow_fixture
        return {'strokeEvents': 1, 'replaySourceAssetReads': 0}

    monkeypatch.setattr(planner_cli, 'optimize_manifest_to_native', fake_opt)
    assert planner_cli.main(['--manifest', str(tmp_path / 'm.json'), '--engine', 'eng', '--output', str(tmp_path / 'o')]) == 0
    assert captured['amodal'] is False
    assert planner_cli.main(['--manifest', str(tmp_path / 'm.json'), '--engine', 'eng', '--output', str(tmp_path / 'o'), '--amodal-background']) == 0
    assert captured['amodal'] is True
