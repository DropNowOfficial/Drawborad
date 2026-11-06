import json
import subprocess
import sys
from pathlib import Path


def run(*args):
    return subprocess.run([sys.executable,'-m','cloud.cli',*map(str,args)],capture_output=True,text=True)


def test_cli_smoke_exports_real_layered_ora(tmp_path):
    folder=tmp_path/'sample'
    result=run('smoke','--output',folder)
    assert result.returncode==0,result.stderr
    doc=json.loads(result.stdout)
    assert doc['backend']=='fixture-no-ai'
    assert Path(doc['ora']).is_file()
    info=run('inspect','--dir',folder)
    assert info.returncode==0
    assert len(json.loads(info.stdout)['layers'])==4


def test_public_binding_rejected_without_explicit_token(monkeypatch):
    monkeypatch.delenv('DRAWBORAD_CLOUD_TOKEN',raising=False)
    r=run('serve','--host','0.0.0.0','--port','8765')
    assert r.returncode != 0
    assert 'TOKEN' in r.stderr.upper()

    monkeypatch.setenv('DRAWBORAD_CLOUD_TOKEN', '   ')
    r = subprocess.run([sys.executable, '-m', 'cloud.cli', 'serve', '--host', '0.0.0.0'],
                       capture_output=True, text=True, timeout=10)
    assert r.returncode != 0 and 'TOKEN' in r.stderr.upper()


def test_cli_segment_requires_actual_model_checkpoint(tmp_path):
    r=run('segment','--image',tmp_path/'missing.png','--output',tmp_path/'out',
          '--prompt','cup')
    assert r.returncode != 0
    assert 'checkpoint' in r.stderr.lower()
