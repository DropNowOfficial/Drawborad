"""Cloud computer boot: real Codespaces start script must expose an authenticated API."""
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import time
import urllib.error
import urllib.request

ROOT=Path(__file__).resolve().parents[2]


def test_codespaces_config_boots_authenticated_service(tmp_path):
    config=json.loads((ROOT/'.devcontainer/devcontainer.json').read_text())
    assert 8765 in config['forwardPorts']
    assert config['portsAttributes']['8765']['visibility']=='private'
    assert 'scripts/cloud/codespaces-start.sh' in config['postStartCommand']
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    root=tmp_path/'data'
    env=dict(os.environ, DRAWBORAD_CLOUD_DATA=str(root), DRAWBORAD_CLOUD_PORT=str(port))
    run=subprocess.run(['bash','scripts/cloud/codespaces-start.sh'],cwd=ROOT,env=env,
                       capture_output=True,text=True,timeout=12)
    assert run.returncode==0,run.stderr
    pid=int((root/'server.pid').read_text())
    try:
        url=f'http://127.0.0.1:{port}'
        for _ in range(40):
            try:
                with urllib.request.urlopen(url+'/healthz',timeout=.4) as res:
                    assert res.status==200
                break
            except (OSError,urllib.error.URLError):
                time.sleep(.15)
        else:
            raise AssertionError('service not available after startup')
        token=(root/'cloud-token').read_text().strip()
        assert len(token)>=32
        try:
            urllib.request.urlopen(url+'/api/v1/jobs',timeout=1)
            raise AssertionError('an unauthenticated request should fail')
        except urllib.error.HTTPError as exc:
            assert exc.code==401
        req=urllib.request.Request(url+'/api/v1/jobs',headers={'Authorization':'Bearer '+token})
        with urllib.request.urlopen(req,timeout=1) as res:
            assert res.status==200
    finally:
        os.kill(pid,signal.SIGTERM)