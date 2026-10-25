#include "Test.h"
#include "codecs/ImageReader.h"
#include "models/ModelManifest.h"
#include "segmentation/ProposalEngine.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
using namespace drawborad;
namespace {
std::filesystem::path environment(const wchar_t* key) {
    wchar_t* value=nullptr;
    if(_wdupenv_s(&value,nullptr,key)!=0 || !value) throw std::runtime_error("missing coffee fixture");
    std::unique_ptr<wchar_t,decltype(&std::free)> owned(value,&std::free);
    return value;
}
}
TEST("M03 native automatic coffee proposals match actual official 32x32 reference") {
    const auto model=validatePackage(environment(L"DRAWBORAD_COFFEE_PACKAGE")); CHECK(model);
    const auto image=decodeImage(environment(L"DRAWBORAD_COFFEE_IMAGE"),{}); CHECK(image);
    CHECK(image.value().decodedPixels);
    AnalysisRequest request; request.model=model.value(); request.sourceSize=image.value().info.orientedSize;
    request.view.rgbTiles={*image.value().decodedPixels};
    const auto begin=std::chrono::steady_clock::now();
    const auto proposals=ProposalEngine{}.proposeAutomatic(request,{}, {},[&](unsigned done,unsigned total){
        if(done%32==0) std::cout<<"native coffee sampled "<<done<<'/'<<total<<" elapsed="
            <<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<"s"<<std::endl;
    });
    if(!proposals) throw std::runtime_error(proposals.error().messageKey+": "+proposals.error().diagnosticId);
    const auto masks=projectMasks(proposals.value(),{request.sourceSize,true}); CHECK(masks);
    const auto output=environment(L"DRAWBORAD_COFFEE_OUTPUT"); std::filesystem::create_directories(output);
    for(std::size_t i=0;i<masks.value().fullResolutionMasks.size();++i){
        std::ofstream stream(output/("auto-mask"+std::to_string(i)+".u8"),std::ios::binary);
        const auto& bytes=masks.value().fullResolutionMasks[i];
        stream.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size())); CHECK(stream.good());
    }
    std::cout<<"native coffee candidates="<<proposals.value().candidates.size()<<" elapsed="
        <<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<"s"<<std::endl;
    CHECK(proposals.value().candidates.size()==8);
    const auto reference=environment(L"DRAWBORAD_COFFEE_AUTO_REFERENCE");
    for(unsigned i=0;i<8;++i){
        const auto expected=decodeImage(reference/("mask0"+std::to_string(i)+".png"),{}); CHECK(expected);
        CHECK(expected.value().decodedPixels);
        const auto& pixels=*expected.value().decodedPixels;
        CHECK(pixels.width==600); CHECK(pixels.height==400);
        double best=0;
        for(const auto& actual:masks.value().fullResolutionMasks){
            std::uint64_t intersected=0,joined=0;
            for(std::size_t y=0;y<400;++y) for(std::size_t x=0;x<600;++x){
                const bool a=actual[y*600+x]>0;
                const bool b=pixels.bytes[y*std::size_t(pixels.rowStride)+x*pixels.format.channelCount()]>0;
                intersected+=a&&b; joined+=a||b;
            }
            const auto iou=joined?double(intersected)/double(joined):1;
            best=std::max(best,iou);
        }
        std::cout<<"official automatic mask="<<i<<" best native IoU="<<best<<std::endl;
        CHECK(best>=.995);
    }
}
