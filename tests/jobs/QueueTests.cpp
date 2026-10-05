#include "Test.h"
#include "jobs/ImageQueue.h"
using namespace drawborad;
TEST("M06 queue defaults to review and enforces one heavy job"){ImageQueue q;std::array<std::filesystem::path,2> p{"a.png","b.png"};auto id=q.enqueue(p,{});CHECK(id);CHECK(q.startAnalysis(0));CHECK(!q.startAnalysis(1));CHECK(q.concurrentHeavyJobs()==1);CHECK(q.analysisComplete(0,true));CHECK(q.items()[0].state==QueueState::WaitingForReview);CHECK(q.startAnalysis(1));CHECK(q.concurrentHeavyJobs()==1);}
TEST("M06 terminal queue item rejects duplicate finish"){ImageQueue q;std::array<std::filesystem::path,1> p{"a.png"};CHECK(q.enqueue(p,{}));CHECK(q.startAnalysis(0));CHECK(q.analysisComplete(0,false));CHECK(q.finish(0,true));CHECK(!q.finish(0,true));}
