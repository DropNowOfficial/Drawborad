import hashlib,json,os,subprocess
from pathlib import Path
from PIL import Image
import pytest

def fixture(root):
    root.mkdir();(root/'masks').mkdir()
    Image.new('RGB',(32,32),'orange').save(root/'source.png')
    objects=[]
    for i in range(2):
        b=bytes(255 if (x<16)==(i==0) else 0 for y in range(32) for x in range(32))
        name=f'masks/layer-{i:02d}.u8';(root/name).write_bytes(b)
        objects.append(dict(name=f'Fixture region {i}',score=1,isBackground=False,pixels=512,mask=name,sha256=hashlib.sha256(b).hexdigest()))
    data=dict(schema=1,backend='fixture-no-ai',promptOrder='back-to-front',image='source.png',sourceSha256=hashlib.sha256((root/'source.png').read_bytes()).hexdigest(),width=32,height=32,objects=objects)
    (root/'manifest.json').write_text(json.dumps(data))
    return root/'manifest.json'

def test_explicit_fixture_and_selected_objects(tmp_path):
    engine=os.environ.get('DRAWBORAD_TEST_ENGINE');assert engine,'Real native engine required'
    manifest=fixture(tmp_path/'input')
    args=[engine,'run','--manifest',str(manifest),'--output',str(tmp_path/'out'),'--accept-index','1']
    assert subprocess.run(args,capture_output=True).returncode!=0
    r=subprocess.run(args+['--fixture'],capture_output=True,text=True)
    assert r.returncode==0,r.stderr
    report=json.loads(r.stdout)
    assert report['backend']=='fixture-no-ai'
    assert report['objectCount']==1 and report['acceptedObjects']==['Fixture region 1']
    assert report['strokeEvents']>0 and report['replaySourceAssetReads']==0
    assert (tmp_path/'out/project.draw').stat().st_size>0
