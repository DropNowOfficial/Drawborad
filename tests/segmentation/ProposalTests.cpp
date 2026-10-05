#include "Test.h"
#include "segmentation/ProposalEngine.h"
using namespace drawborad;
TEST("M03 projected masks preserve source dimensions and explicit unassigned region"){AnalysisRequest r;r.sourceSize={16,8};r.model.taskId="sam21.image.v1";r.model.sha256.bytes[0]=9;r.prompts.boxes.push_back({2,2,6,6});ProposalEngine e;auto p=e.propose(r);CHECK(p);auto m=projectMasks(p.value(),{{16,8},true});CHECK(m);CHECK(m.value().dimensions==r.sourceSize);CHECK(m.value().unassigned.size()==128);CHECK(std::count(m.value().unassigned.begin(),m.value().unassigned.end(),255)>0);}
