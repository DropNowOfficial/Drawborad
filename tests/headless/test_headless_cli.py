#!/usr/bin/env python3
"""Behavioral contract for the Linux headless executable."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from verify_artifacts import verify
from PIL import Image

exe = sys.argv[1]

def run(*args):
    return subprocess.run([exe, *map(str, args)], text=True, capture_output=True, timeout=45)

result = run('--help')
assert result.returncode == 0, result.stderr
assert 'run' in result.stdout and 'smoke' in result.stdout
for args in [(), ('smoke',), ('run',), ('unknown',), ('smoke', '--output'), ('smoke', '--bad', 'x')]:
    assert run(*args).returncode == 2, args
with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    output = root / '绘画 output'
    result = run('smoke', '--output', output)
    assert result.returncode == 0, result.stderr
    report = json.loads(result.stdout)
    assert report['backend'] == 'fixture-no-ai'
    assert report['objectCount'] == 2
    assert report['wholeImageFidelityVerified'] is False
    assert report['naturalProcessReviewed'] is False
    assert len(report['gitSha']) == 40
    verify(output)
    original = (output / 'project.draw').read_bytes()
    assert run('smoke', '--output', output).returncode != 0
    assert (output / 'project.draw').read_bytes() == original
    other = root / 'second'
    assert run('smoke', '--output', other).returncode == 0
    assert (output / 'final.png').read_bytes() == (other / 'final.png').read_bytes()
    manifest = root / 'manifest.json'
    manifest.write_text(json.dumps({'schema': 1, 'backend': 'fixture-no-ai'}))
    for accept in [[], ['--accept-all']]:
        result = run('run', '--manifest', manifest, '--output', root / 'invalid', *accept)
        assert result.returncode != 0
        assert not (root / 'invalid').exists()
    # Contract fixture only: backend is the loader's required schema discriminator.
    # No SAM inference is used; this temporary fixture is not shipped as evidence.
    contract = root / '契约 fixture'
    (contract / 'masks').mkdir(parents=True)
    image = Image.new('RGBA', (64, 64))
    image.putdata([(180, 60, 40, 255) if x < 32 else (40, 100, 180, 255)
                   for y in range(64) for x in range(64)])
    source = contract / 'source.png'
    image.save(source)
    objects = []
    for index in range(2):
        mask = bytes(255 if (x < 32) == (index == 0) else 0
                     for y in range(64) for x in range(64))
        relative = f'masks/layer-{index:02d}.u8'
        (contract / relative).write_bytes(mask)
        objects.append({'name': f'Contract fixture {index}', 'score': 1,
                        'isBackground': False, 'pixels': 2048, 'mask': relative,
                        'sha256': hashlib.sha256(mask).hexdigest()})
    manifest = contract / 'manifest.json'
    manifest.write_text(json.dumps({
        'schema': 1, 'backend': 'sam3-image', 'promptOrder': 'back-to-front',
        'image': 'source.png', 'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'width': 64, 'height': 64, 'objects': objects,
        'testProvenance': 'synthetic-contract-fixture-no-ai',
    }))
    contract_output = contract / '绘画 output'
    assert run('run', '--manifest', manifest, '--output', contract_output).returncode == 2
    assert not contract_output.exists()
    result = run('run', '--manifest', manifest, '--output', contract_output, '--accept-all')
    assert result.returncode == 0, result.stderr
    assert json.loads(result.stdout)['objectCount'] == 2
    verify(contract_output)
print('Headless CLI and independent ORA assertions passed')
