import numpy as np
import pytest
from cloud.batch.sam3_worker import project_candidate,Candidate

def test_helmet_instances_in_distinct_views_remain_distinct():
    item=Candidate('helmet',.8,np.ones((2,2),dtype=bool))
    a=project_candidate(item,{'id':'front','box':[0,0,2,2]},(2,4))
    b=project_candidate(item,{'id':'detail','box':[2,0,4,2]},(2,4))
    assert a.name=='front/helmet' and b.name=='detail/helmet'
    assert not np.any(a.mask&b.mask) and np.all(a.mask|b.mask)
    with pytest.raises(ValueError):project_candidate(item,{'id':'bad','box':[0,0,3,2]},(2,4))
