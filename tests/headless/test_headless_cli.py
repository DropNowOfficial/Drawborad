#!/usr/bin/env python3
"""Behavioral contract for the Linux headless executable."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from verify_artifacts import verify

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
print('Headless CLI and independent ORA assertions passed')
