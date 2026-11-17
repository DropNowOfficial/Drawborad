import io,json,os
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import pytest
from PIL import Image
from cloud.batch.jobs import Jobs,Conflict,LocalStore

def photo():
    b=io.BytesIO();Image.new('RGB',(32,32),'orange').save(b,format='PNG');return b.getvalue()

def test_planner_choice_survives_worker_restart(tmp_path):
    jobs=Jobs(LocalStore(tmp_path),planner='multiscale')
    job=jobs.create(photo(),'quality-job','fixture-no-ai')
    assert job['planner']=='multiscale'
    assert Jobs(LocalStore(tmp_path)).get(job['id'])['planner']=='multiscale'
    with pytest.raises(ValueError):Jobs(LocalStore(tmp_path),planner='unknown')

def test_upload_idempotency_and_no_implicit_acceptance(tmp_path):
    jobs=Jobs(LocalStore(tmp_path))
    a=jobs.create(photo(),'request-a','fixture-no-ai')
    assert a==jobs.create(photo(),'request-a','fixture-no-ai')
    with pytest.raises(Conflict):jobs.create(photo(),'request-a','sam3-image',prompts=['cat'])
    with pytest.raises(Conflict):jobs.accept(a['id'],['invented'])
    jobs.segment(a['id'])
    recovered=Jobs(LocalStore(tmp_path)).get(a['id'])
    assert recovered['state']=='awaiting_review' and recovered['inference_ran'] is False
    assert not recovered['accepted'] and recovered['backend']=='fixture-no-ai'
    with pytest.raises(ValueError):jobs.accept(a['id'],[])
    with pytest.raises(ValueError):jobs.accept(a['id'],['invented'])
    chosen=[recovered['candidates'][1]['id']]
    jobs.accept(a['id'],chosen)
    assert jobs.get(a['id'])['accepted']==chosen
    with pytest.raises(Conflict):jobs.accept(a['id'],chosen)

def test_only_one_worker_claims_and_cancel_wins(tmp_path):
    jobs=Jobs(LocalStore(tmp_path));j=jobs.create(photo(),'r','fixture-no-ai')
    def claim(_):
        try:return jobs.claim(j['id'],'segment')['state']
        except Conflict:return 'conflict'
    with ThreadPoolExecutor(4) as ex:values=list(ex.map(claim,range(4)))
    assert values.count('segment_running')==1
    jobs.cancel(j['id']);assert Jobs(LocalStore(tmp_path)).get(j['id'])['state']=='cancelled'
    with pytest.raises(Conflict):jobs.claim(j['id'],'segment')

def test_corrupt_asset_rejected(tmp_path):
    jobs=Jobs(LocalStore(tmp_path));j=jobs.create(photo(),'r','fixture-no-ai')
    record=j['input'];jobs.store.put(record['key'],b'corrupted')
    with pytest.raises(ValueError):jobs.read_asset(record)

def test_native_artifacts_survive_worker_and_source_removal(tmp_path):
    engine=os.environ.get('DRAWBORAD_TEST_ENGINE')
    if not engine:pytest.skip('Native engine integration runs in CI')
    jobs=Jobs(LocalStore(tmp_path/'store'),engine=engine)
    j=jobs.create(photo(),'native','fixture-no-ai');jobs.segment(j['id'])
    j=jobs.get(j['id']);jobs.accept(j['id'],[j['candidates'][1]['id']]);jobs.build(j['id'])
    j=Jobs(LocalStore(tmp_path/'store')).get(j['id'])
    assert j['state']=='completed',j
    report=json.loads(jobs.read_asset(j['outputs']['report.json']))
    assert report['strokeEvents']>0 and report['objectCount']==1
    assert report['backend']=='fixture-no-ai' and report['replaySourceAssetReads']==0
    assert j['cost']['actual_usd'] is None and j['cost']['gpu_inference_ran'] is False
    # Copy outputs alone; all worker temporary directories have already been destroyed.
    out=tmp_path/'export';out.mkdir()
    for name,asset in j['outputs'].items():(out/name).write_bytes(jobs.read_asset(asset))
    import sys
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'headless'))
    from verify_artifacts import verify
    verify(out)

def test_deadline_and_late_result_do_not_stay_running(tmp_path):
    jobs=Jobs(LocalStore(tmp_path),timeout=-1);j=jobs.create(photo(),'late','fixture-no-ai')
    with pytest.raises(Conflict):jobs.segment(j['id'])
    assert jobs.get(j['id'])['state']=='timed_out'

def test_confirmed_views_cannot_overlap_or_merge_identity(tmp_path):
    jobs=Jobs(LocalStore(tmp_path))
    with pytest.raises(ValueError):jobs.create(photo(),'views','sam3-image',['helmet'],[{'id':'front','box':[0,0,20,32],'confirmed':True},{'id':'back','box':[10,0,32,32],'confirmed':True}])

def test_crashed_worker_expires_when_status_is_read(tmp_path):
    jobs=Jobs(LocalStore(tmp_path),timeout=-1);j=jobs.create(photo(),'crash','fixture-no-ai')
    jobs.claim(j['id'],'segment')
    assert Jobs(LocalStore(tmp_path)).get(j['id'])['state']=='timed_out'

def test_failed_real_model_attempt_keeps_billing_unknown(tmp_path):
    jobs=Jobs(LocalStore(tmp_path));j=jobs.create(photo(),'sam-fail','sam3-image',['cat'])
    def fails(*_):raise RuntimeError('model load failed')
    with pytest.raises(RuntimeError):jobs.segment(j['id'],fails)
    cost=jobs.get(j['id'])['cost']
    assert cost['actual_usd'] is None and cost['billing_status']=='unreconciled'

@pytest.mark.parametrize('bad_id',[7,True,None])
def test_view_ids_must_be_strings_before_paid_work(tmp_path,bad_id):
    jobs=Jobs(LocalStore(tmp_path))
    with pytest.raises(ValueError):jobs.create(photo(),'view-type','sam3-image',['cat'],[{'id':bad_id,'box':[0,0,16,32],'confirmed':True}])
