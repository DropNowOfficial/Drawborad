#include "Test.h"
#include "segmentation/ProposalEngine.h"
#include <algorithm>
#include <cstring>
#include <stop_token>
using namespace drawborad;
namespace {
Result<MaskSet> runtimeProjection(PixelSize size, const std::vector<float>& logits) {
    AnalysisRequest request;
    request.sourceSize = size;
    request.model.taskId = "sam21.image.v1";
    request.prompts.points = {{.5, .5, true}};
    TileData image;
    image.width = std::uint32_t(size.width); image.height = std::uint32_t(size.height);
    image.format = {ColorMode::RGB, Depth::U8, false};
    image.rowStride = size.width * 3;
    image.bytes.assign(std::size_t(size.width * size.height * 3), 128);
    request.view.rgbTiles = {image};
    ProposalEngine engine([&](const ModelManifest&, const InferenceRequest&, std::stop_token) {
        Bytes mask(logits.size() * sizeof(float)), score(sizeof(float));
        std::memcpy(mask.data(), logits.data(), mask.size());
        const float confidence = .9f;
        std::memcpy(score.data(), &confidence, sizeof(float));
        return Result<InferenceOutput>{InferenceOutput{{std::move(mask), std::move(score)}, 0, 0}};
    });
    const auto proposal = engine.propose(request);
    if (!proposal) return proposal.error();
    return projectMasks(proposal.value(), {size, true});
}
TEST("M03 automatic grid filters unstable masks and deduplicates overlapping proposals") {
    AnalysisRequest request;
    request.sourceSize={8,8}; request.model.taskId="sam21.image.v1";
    TileData image; image.width=8; image.height=8; image.rowStride=24;
    image.format={ColorMode::RGB,Depth::U8,false}; image.bytes.assign(8*8*3,128);
    request.view.rgbTiles={image};
    unsigned calls=0, progress=0;
    ProposalEngine engine([&](const ModelManifest&,const InferenceRequest& input,std::stop_token){
        ++calls;
        CHECK(input.tensorRefs[1].size()==2*sizeof(float));
        std::vector<float> logits(3*256*256,-2);
        for(std::size_t c=0;c<3;++c) for(int y=64;y<192;++y) for(int x=64;x<192;++x)
            logits[c*256*256+std::size_t(y*256+x)]=c==2?.5f:2.0f;
        std::array<float,3> scores{.7f,.9f,.95f};
        InferenceOutput output; output.tensorRefs.resize(2);
        output.tensorRefs[0].resize(logits.size()*sizeof(float));
        output.tensorRefs[1].resize(sizeof(scores));
        std::memcpy(output.tensorRefs[0].data(),logits.data(),output.tensorRefs[0].size());
        std::memcpy(output.tensorRefs[1].data(),scores.data(),sizeof(scores));
        return Result<InferenceOutput>{std::move(output)};
    });
    AutomaticProposalOptions options; options.pointsPerSide=2;
    const auto result=engine.proposeAutomatic(request,options,{},[&](unsigned done,unsigned total){
        CHECK(total==4); CHECK(done==++progress);
    });
    CHECK(result);
    CHECK(progress==4);
    CHECK(calls==5); // Four independent prompts, then recover the one retained mask.
    CHECK(result.value().candidates.size()==1);
    CHECK(std::abs(result.value().candidates[0].score-.9)<.0001);
    const auto masks=projectMasks(result.value(),{{8,8},true});
    CHECK(masks);
    CHECK(std::count(masks.value().fullResolutionMasks[0].begin(),masks.value().fullResolutionMasks[0].end(),255)==16);
    CHECK(std::count(masks.value().unassigned.begin(),masks.value().unassigned.end(),255)==48);
    CHECK(request.prompts.points.empty());
    std::stop_source stop;
    const auto cancelled=engine.proposeAutomatic(request,options,stop.get_token(),[&](unsigned,unsigned){stop.request_stop();});
    CHECK(!cancelled); CHECK(cancelled.error().code==ErrorCode::Cancelled);
}
}
TEST("M03 runtime mask uses full image coordinates rather than stretching its bounds") {
    std::vector<float> logits(256 * 256, -1);
    for (int y = 128; y < 192; ++y) for (int x = 32; x < 96; ++x)
        logits[std::size_t(y * 256 + x)] = 1;
    const auto projected = runtimeProjection({8, 8}, logits);
    CHECK(projected);
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
        CHECK((projected.value().fullResolutionMasks[0][std::size_t(y * 8 + x)] > 0) ==
              (x >= 1 && x < 3 && y >= 4 && y < 6));
}
TEST("M03 runtime logits are interpolated before thresholding at source resolution") {
    std::vector<float> logits(256 * 256, -2);
    logits[127 * 256 + 127] = 1;
    logits[127 * 256 + 128] = 1;
    logits[128 * 256 + 127] = 1;
    // Pixel centre samples four logits: (1+1+1-2)/4 > 0. A pre-thresholded
    // nearest sample instead selects the negative bottom-right value.
    const auto projected = runtimeProjection({1, 1}, logits);
    CHECK(projected);
    CHECK(projected.value().fullResolutionMasks[0][0] == 255);
}
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

TEST("M03 missing runtime and manual box cannot fake a full-image proposal"){
    AnalysisRequest request;
    request.sourceSize={16,16};
    request.model.taskId="sam21.image.v1";
    request.model.sha256.bytes[0]=1;
    ProposalEngine engine;
    auto result=engine.propose(request);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedFeature);
}

TEST("M03 selected runtime with missing image never falls back to a manual box") {
    AnalysisRequest request;
    request.sourceSize = {16, 16};
    request.model.taskId = "sam21.image.v1";
    request.model.resolvedWeights = "selected-encoder.onnx";
    request.model.resolvedDecoderWeights = "selected-decoder.onnx";
    request.prompts.boxes.push_back({2, 2, 12, 12});
    const auto result = ProposalEngine{}.propose(request);
    CHECK(!result);
    CHECK(result.error().code == ErrorCode::InvalidInput);
    CHECK(result.error().messageKey == "proposal.analysis-image-required");
}

TEST("M03 explicit inference runner requires image even when manual boxes exist") {
    AnalysisRequest request;
    request.sourceSize = {16, 16};
    request.model.taskId = "sam21.image.v1";
    request.prompts.boxes.push_back({2, 2, 12, 12});
    bool invoked = false;
    ProposalEngine engine([&](const ModelManifest&, const InferenceRequest&, std::stop_token) {
        invoked = true;
        return Result<InferenceOutput>{error(ErrorCode::InvalidInput, "test.runtime-must-not-run")};
    });
    const auto result = engine.propose(request);
    CHECK(!result);
    CHECK(result.error().code == ErrorCode::InvalidInput);
    CHECK(result.error().messageKey == "proposal.analysis-image-required");
    CHECK(!invoked);
}
