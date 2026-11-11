import io
from PIL import Image
from fastapi.testclient import TestClient
from cloud.batch.jobs import Jobs,LocalStore
from cloud.batch.api import create_app

def test_review_api_requires_auth_and_explicit_selection(tmp_path):
    jobs=Jobs(LocalStore(tmp_path));calls=[]
    def dispatch(jid,phase):
        calls.append((jid,phase))
        if phase=='segment':jobs.segment(jid)
    client=TestClient(create_app(jobs,'owner-test',dispatch,allow_fixture=True))
    assert client.get('/api/v1/generation/capabilities').status_code==401
    client.headers['Authorization']='Bearer owner-test'
    b=io.BytesIO();Image.new('RGB',(32,32),'blue').save(b,format='PNG')
    r=client.post('/api/v1/generation/jobs',headers={'Idempotency-Key':'one'},files={'image':('photo.png',b.getvalue())},data={'backend':'fixture-no-ai'})
    assert r.status_code==201,r.text
    jid=r.json()['id'];p='/api/v1/generation/jobs/'+jid
    assert client.post(p+'/start').status_code==202
    state=client.get(p).json();assert state['state']=='awaiting_review'
    assert client.get(p+'/candidates/'+state['candidates'][0]['id']+'.png').status_code==200
    assert client.post(p+'/accept',json={'ids':[]}).status_code==422
    assert client.post(p+'/accept',json={'ids':[state['candidates'][0]['id']]}).status_code==200
    assert client.post(p+'/build').status_code==202
    assert calls==[(jid,'segment'),(jid,'build')]
    assert client.get(p+'/files/project.draw').status_code==409
    assert client.post(p+'/cancel').status_code==200

def test_invalid_upload_and_review_payload_are_client_errors(tmp_path):
    client=TestClient(create_app(Jobs(LocalStore(tmp_path)),'test',lambda *_:None,True),raise_server_exceptions=False)
    client.headers['Authorization']='Bearer test'
    r=client.post('/api/v1/generation/jobs',headers={'Idempotency-Key':'bad'},files={'image':('bad.png',b'not an image')},data={'backend':'fixture-no-ai'})
    assert r.status_code==422,r.text
    r=client.post('/api/v1/generation/jobs/'+'0'*32+'/accept',json=[])
    assert r.status_code==422,r.text
