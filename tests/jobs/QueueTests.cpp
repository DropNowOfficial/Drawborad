#include "Test.h"
#include "jobs/ImageQueue.h"
using namespace drawborad;
TEST("M06 queue defaults to review and enforces one heavy job"){ImageQueue q;std::array<std::filesystem::path,2> p{"a.png","b.png"};auto id=q.enqueue(p,{});CHECK(id);CHECK(q.startAnalysis(0));CHECK(!q.startAnalysis(1));CHECK(q.concurrentHeavyJobs()==1);CHECK(q.analysisComplete(0,true));CHECK(q.items()[0].state==QueueState::WaitingForReview);CHECK(q.startAnalysis(1));CHECK(q.concurrentHeavyJobs()==1);}
