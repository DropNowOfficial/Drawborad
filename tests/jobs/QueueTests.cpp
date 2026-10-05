#include "Test.h"
#include "jobs/ImageQueue.h"
#include "jobs/QueueStore.h"
#include <fstream>
using namespace drawborad;
TEST("M06 queue defaults to review and enforces one heavy job"){ImageQueue q;std::array<std::filesystem::path,2> p{"a.png","b.png"};auto id=q.enqueue(p,{});CHECK(id);CHECK(q.startAnalysis(0));CHECK(!q.startAnalysis(1));CHECK(q.concurrentHeavyJobs()==1);CHECK(q.analysisComplete(0,true));CHECK(q.items()[0].state==QueueState::WaitingForReview);CHECK(q.startAnalysis(1));CHECK(q.concurrentHeavyJobs()==1);}
TEST("M06 terminal queue item rejects duplicate finish"){ImageQueue q;std::array<std::filesystem::path,1> p{"a.png"};CHECK(q.enqueue(p,{}));CHECK(q.startAnalysis(0));CHECK(q.analysisComplete(0,false));CHECK(q.finish(0,true));CHECK(!q.finish(0,true));}

TEST("M06 queue survives restart without duplicate heavy work"){
    ImageQueue q;
    std::array<std::filesystem::path,2> p{"一.png","b.png"};
    CHECK(q.enqueue(p,{}));
    CHECK(q.startAnalysis(0));
    CHECK(q.analysisComplete(0,true));
    CHECK(q.acceptReview(0,42));
    CHECK(q.concurrentHeavyJobs()==1);
    auto file=std::filesystem::temp_directory_path()/"drawborad-queue-restart.bin";
    CHECK(QueueStore::save(q,file));
    auto restored=QueueStore::load(file);
    CHECK(restored);
    CHECK(restored.value().concurrentHeavyJobs()==0);
    CHECK(restored.value().items()[0].state==QueueState::WaitingForReview);
    CHECK(restored.value().items()[0].inputRevision==42);
    CHECK(restored.value().items()[1].state==QueueState::Queued);
    CHECK(restored.value().acceptReview(0,42));
    CHECK(restored.value().concurrentHeavyJobs()==1);
    CHECK(restored.value().finish(0,true));
    CHECK(QueueStore::save(restored.value(),file));
    auto terminal=QueueStore::load(file);
    CHECK(terminal);
    CHECK(terminal.value().items()[0].state==QueueState::Done);
    CHECK(!terminal.value().finish(0,true));
    std::filesystem::remove(file);
}
TEST("M06 corrupt queue store is rejected"){
    auto file=std::filesystem::temp_directory_path()/"drawborad-queue-corrupt.bin";
    std::ofstream(file,std::ios::binary)<<"bad";
    auto restored=QueueStore::load(file);
    CHECK(!restored);
    CHECK(restored.error().code==ErrorCode::CorruptData);
    std::filesystem::remove(file);
}
