import pytest
from cloud.batch.jobs import LocalStore,Conflict
from cloud.batch.providers import RunPodDispatcher

class LostReply:
    def __init__(self):self.calls=0
    def post(self,*a,**kw):self.calls+=1;raise TimeoutError('response lost')

def test_uncertain_submission_never_automatically_resubmits(tmp_path):
    client=LostReply();store=LocalStore(tmp_path)
    dispatch=RunPodDispatcher(store,'endpoint','private-test-token',client=client,approved_usd='10')
    with pytest.raises(TimeoutError):dispatch('a'*32,'segment')
    with pytest.raises(Conflict):dispatch('a'*32,'segment')
    assert client.calls==1
    with pytest.raises(Conflict):dispatch('b'*32,'segment')
    assert client.calls==1

def test_no_budget_or_cpu_endpoint_never_calls_provider(tmp_path):
    client=LostReply()
    for budget,phase in [('0','segment'),('11','segment'),('10','build')]:
        d=RunPodDispatcher(LocalStore(tmp_path/budget),'endpoint','token',client=client,approved_usd=budget)
        with pytest.raises(ValueError):d('c'*32,phase)
    assert client.calls==0
