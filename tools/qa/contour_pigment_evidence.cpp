// Portable native-render evidence. Build with the same sources as test_E02,
// replacing TestMain.cpp and PlanningTests.cpp with this file.
#include "RenderFixture.h"
#include "reconstruct/Planning.h"
#include "render/ReferenceRenderer.h"
#include "view/LayerTree.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace drawborad;
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const std::filesystem::path output(argv[1]);std::filesystem::create_directories(output);
    auto snapshot=fixture::snapshot(ColorMode::RGB,Depth::U8,64,64);
    TileData source;source.width=64;source.height=64;source.format=snapshot.spec.colorSpec.format;
    source.profileId=snapshot.spec.colorSpec.profileId;source.rowStride=256;source.bytes.resize(64*64*4);
    Bytes maskBytes(64*64,0);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        const auto p=std::size_t(y*64+x);
        source.bytes[p*4]=40;source.bytes[p*4+1]=110;source.bytes[p*4+2]=180;source.bytes[p*4+3]=255;
        if(x>=8 && x<56 && y>=8 && y<56 && (x<20 || x>=44 || y<20 || y>=44))maskBytes[p]=255;
    }
    auto resources=std::make_shared<ResourceMap>(*snapshot.resources);
    Resource mask{sha256(maskBytes),ResourceKind::Mask,std::make_shared<const Bytes>(maskBytes),{}};
    resources->emplace(mask.id,mask);snapshot.resources=resources;
    auto nodes=std::make_shared<NodeMap>(*snapshot.nodes);nodes->at(Id::fromNumber(2)).maskId=mask.id;snapshot.nodes=nodes;
    PlanningRegion region{snapshot,Id::fromNumber(2),Id::fromNumber(3),{0,0,64,64},maskBytes,&source,1,8192,250000,{},true};
    const auto plan=planBasePigment(region,StyleSettings{"marker",2,1,1,.04,true});
    if(!plan) {std::cerr<<plan.error().messageKey<<'\n';return 1;}
    snapshot=fixture::withEvents(snapshot,plan.value().events);
    std::size_t samples=0;for(const auto& event:plan.value().events)samples+=event.geometry.size();
    std::cout<<"events="<<plan.value().events.size()<<" samples="<<samples<<'\n';
    for(const auto percent:{0u,25u,50u,75u,100u}) {
        const auto count=(plan.value().events.size()*percent+99)/100;
        ReferenceRenderer renderer;const auto raw=renderer.renderRegion(snapshot,region.layerId,{0,0,64,64},{snapshot.revision,count});
        if(!raw)return 1;
        const auto clipped=nativeMaskedTile(snapshot,region.layerId,raw.value(),0,0);if(!clipped)return 1;
        std::ofstream ppm(output/(std::to_string(percent)+".ppm"),std::ios::binary);ppm<<"P6\n64 64\n255\n";
        for(std::size_t p=0;p<maskBytes.size();++p) {
            const auto a=clipped.value().bytes[p*4+3];
            for(unsigned c=0;c<3;++c)ppm.put(char((unsigned(clipped.value().bytes[p*4+c])*a+255*(255-a)+127)/255));
        }
        if(!ppm)return 1;
        std::cout<<percent<<"% events="<<count<<" sourceAssetReads="<<renderer.statistics().sourceAssetReads<<'\n';
    }
}
