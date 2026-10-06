#include "Test.h"
#include "segmentation/ProposalEngine.h"
#include <algorithm>
using namespace drawborad;
TEST("M03 projected masks preserve source dimensions and explicit unassigned region"){AnalysisRequest r;r.sourceSize={16,8};r.model.taskId="sam21.image.v1";r.model.sha256.bytes[0]=9;r.prompts.boxes.push_back({2,2,6,6});ProposalEngine e;auto p=e.propose(r);CHECK(p);auto m=projectMasks(p.value(),{{16,8},true});CHECK(m);CHECK(m.value().dimensions==r.sourceSize);CHECK(m.value().unassigned.size()==128);CHECK(std::count(m.value().unassigned.begin(),m.value().unassigned.end(),255)>0);}

TEST("M03 projection honors analysis transform and source alpha without mutating source"){
    AnalysisRequest r;
    r.sourceSize={8,8};
    r.model.taskId="sam21.image.v1";
    r.model.sha256.bytes[0]=7;
    r.view.sourceTransform.affine={2,0,0,2,0,0};
    Bytes alpha(64,255);
    alpha[std::size_t(2*8+2)]=0;
    r.view.sourceAlpha={alpha};
    TileData source;
    source.width=8; source.height=8; source.format={ColorMode::RGB,Depth::U8,true}; source.rowStride=32; source.bytes.assign(256,17);
    r.view.rgbTiles.push_back(source);
    const auto sourceBefore=r.view.rgbTiles[0].bytes;
    r.prompts.boxes.push_back({1,1,4,4});
    ProposalEngine engine;
    auto proposals=engine.propose(r);
    CHECK(proposals);
    CHECK(r.view.rgbTiles[0].bytes==sourceBefore);
    auto masks=projectMasks(proposals.value(),{{8,8},true});
    CHECK(masks);
    CHECK(masks.value().fullResolutionMasks.size()==1);
    const auto& m=masks.value().fullResolutionMasks[0];
    CHECK(m[std::size_t(1*8+1)]==255);
    CHECK(m[std::size_t(2*8+2)]==0);
    CHECK(m[std::size_t(0)]==0);
    CHECK(masks.value().unassigned[std::size_t(0)]==255);
    CHECK(masks.value().unassigned[std::size_t(2*8+2)]==255);
}
TEST("M03 malformed low resolution mask is rejected"){
    ProposalSet p;
    p.sourceSize={4,4};
    p.candidates.push_back({Id::fromNumber(1),{0,0,4,4},Bytes(12,255),0});
    auto result=projectMasks(p,{{4,4},true});
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::CorruptData);
}

TEST("M03 runtime path normalizes image prompts and decodes low resolution masks"){
    AnalysisRequest r;
    r.sourceSize={2,2};
    r.model.taskId="sam21.image.v1";
    r.model.resolvedWeights="verified-encoder.onnx";
    r.model.resolvedDecoderWeights="verified-decoder.onnx";
    r.model.sha256.bytes[0]=42;
    TileData image;
    image.width=2;
    image.height=2;
    image.format={ColorMode::RGB,Depth::U8,false};
    image.rowStride=6;
    image.bytes={255,0,0, 0,255,0, 0,0,255, 255,255,255};
    r.view.rgbTiles={image};
    r.prompts.points={{1,1,true}};
    r.prompts.boxes={{0,0,2,2}};

    bool invoked=false;
    ProposalEngine engine([&](const ModelManifest&,const InferenceRequest& request,std::stop_token){
        invoked=true;
        CHECK(request.tensorRefs.size()==5);
        CHECK(request.tensorRefs[0].size()==std::size_t(1*3*1024*1024*4));
        CHECK(request.tensorRefs[1].size()==std::size_t(1*3*2*4));
        CHECK(request.tensorRefs[2].size()==std::size_t(1*3*8));
        const auto* labels=reinterpret_cast<const std::int64_t*>(request.tensorRefs[2].data());
        CHECK(labels[0]==2);
        CHECK(labels[1]==3);
        CHECK(labels[2]==1);
        CHECK(request.tensorRefs[3].size()==std::size_t(1*1*256*256*4));
        CHECK(request.tensorRefs[4].size()==4);

        InferenceOutput out;
        std::vector<float> mask(256*256,-1.0f);
        for(int y=64;y<192;y++)for(int x=64;x<192;x++)mask[std::size_t(y*256+x)]=1.0f;
        out.tensorRefs.resize(2);
        out.tensorRefs[0].resize(mask.size()*sizeof(float));
        std::memcpy(out.tensorRefs[0].data(),mask.data(),out.tensorRefs[0].size());
        float score=.875f;
        out.tensorRefs[1].resize(sizeof(score));
        std::memcpy(out.tensorRefs[1].data(),&score,sizeof(score));
        return Result<InferenceOutput>{std::move(out)};
    });
    auto proposals=engine.propose(r);
    CHECK(proposals);
    CHECK(invoked);
    CHECK(proposals.value().candidates.size()==1);
    CHECK(proposals.value().candidates[0].lowResMask.size()==256*256);
    CHECK(proposals.value().candidates[0].score>.87);
    CHECK(proposals.value().candidates[0].score<.88);
    CHECK(proposals.value().candidates[0].lowResMask[128*256+128]==255);
    CHECK(proposals.value().candidates[0].lowResMask[0]==0);
    CHECK(proposals.value().sourceToAnalysis.affine[0]==512);
    CHECK(proposals.value().sourceToAnalysis.affine[3]==512);
}
