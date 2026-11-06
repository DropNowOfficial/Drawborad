"""Real persistent native session contract, no mocked engine."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    subprocess.run([binary, 'smoke', '--output', str(root/'generated')], check=True, capture_output=True)
    proc = subprocess.Popen([binary, 'session'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    seq = 0
    def call(method, **params):
        global seq
        seq += 1
        proc.stdin.write(json.dumps(dict(request_id=str(seq), method=method, params=params))+'\n')
        proc.stdin.flush()
        line = proc.stdout.readline()
        assert line, 'Native persistent session must return a JSON response'
        r = json.loads(line)
        assert r['request_id'] == str(seq)
        return r
    def ok(method, **params):
        r = call(method, **params)
        assert 'result' in r, r
        return r['result']
    def command(state, **params):
        return ok('command', expected_revision=state['revision'], **params)
    try:
        state = ok('open', path=str(root/'generated/project.draw'))
        assert state['strokes'] and not state['dirty']
        # The earliest guide may be covered in the final composite. Verify
        # its edit at its replay position, then use the last visible event.
        first = state['strokes'][0]
        ok('export', path=str(root/'guide-before.png'), format='png', event_limit='1')
        guide = command(state, type='recolor_stroke', stroke_id=first['id'], color='#00ff00')
        assert guide['strokes'][0]['color'] != first['color']
        ok('export', path=str(root/'guide-after.png'), format='png', event_limit='1')
        assert (root/'guide-before.png').read_bytes() != (root/'guide-after.png').read_bytes()
        state = ok('undo', expected_revision=guide['revision'])
        event = state['strokes'][-1]
        before = ok('export', path=str(root/'before.png'), format='png')
        changed = command(state, type='recolor_stroke', stroke_id=event['id'], color='#00ff00')
        assert changed['revision'] != state['revision'] and changed['dirty']
        assert changed['strokes'][-1]['id'] == event['id']
        ok('export', path=str(root/'after.png'), format='png')
        assert (root/'before.png').read_bytes() != (root/'after.png').read_bytes()
        stale = call('command', expected_revision=state['revision'], type='recolor_stroke', stroke_id=event['id'], color='#ff00ff')
        assert stale['error']['code'] == 'stale_revision'
        assert ok('state')['revision'] == changed['revision']
        undone = ok('undo', expected_revision=changed['revision'])
        ok('export', path=str(root/'undo.png'), format='png')
        assert (root/'before.png').read_bytes() == (root/'undo.png').read_bytes()
        redone = ok('redo', expected_revision=undone['revision'])
        locked = command(redone, type='set_properties', node_id=event['object_id'], locked=True)
        bad = call('command', expected_revision=locked['revision'], type='recolor_stroke', stroke_id=event['id'], color='#ff0000')
        assert bad['error']['code']=='locked'
        assert ok('state')['revision']==locked['revision']
        unlocked = command(locked, type='set_properties', node_id=event['object_id'], locked=False)
        saved = ok('save', expected_revision=unlocked['revision'], path=str(root/'saved.draw'))
        assert not saved['dirty'] and saved['saved_revision']==saved['revision']
        ok('close', expected_revision=saved['revision'])
        reopened = ok('open', path=str(root/'saved.draw'))
        assert reopened['strokes']==saved['strokes']
        ok('export', path=str(root/'reopen.png'), format='png')
        assert (root/'after.png').read_bytes()==(root/'reopen.png').read_bytes()
        ok('export', path=str(root/'zero.png'), format='png', event_limit='0')
        assert (root/'zero.png').read_bytes()!=(root/'reopen.png').read_bytes()
        result=ok('export', path=str(root/'painted.ora'), format='ora')
        assert result['source_asset_reads']=='0'
        print('PASS native session: recolor/render/undo/redo/lock/stale/save/reopen/source-free replay/export')
    finally:
        proc.stdin.close()
        proc.wait(timeout=10)
