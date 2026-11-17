// Original-coordinate native planning probe. Raw RGBA input is diagnostic only;
// production image/profile/provenance validation remains in HeadlessWorkflow.
#include "reconstruct/Planning.h"
#include "core/Commands.h"
#include "render/ReferenceRenderer.h"
#include "storage/ProjectStore.h"
#include <fstream>
#include <iostream>
using namespace drawborad;
int main(int argc,char** argv){
    if(argc!=7)return 2;
    try{
        const auto w=std::stoul(argv[2]),h=std::stoul(argv[3]);
        if(!w || !h || w*h>4*1024*1024)return 2;
        NewDocumentRequest request;request.dimensions={double(w),double(h),Unit::Px,300};
        auto spec=validateDocument(request,{});if(!spec)return 3;
        auto doc=Document::create(spec.value(),Id::fromNumber(100));if(!doc)return 3;
        Bytes mask(w*h,255);Resource res{sha256(mask),ResourceKind::Mask,std::make_shared<const Bytes>(mask),{}};
        LayerNode object;object.id=Id::fromNumber(2);object.parent=Document::rootId();object.kind=NodeKind::Object;object.name="Diagnostic whole-image mask (not semantic)";object.maskId=res.id;object.bounds={0,0,w,h};
        LayerNode layer;layer.id=Id::fromNumber(3);layer.parent=object.id;layer.objectId=object.id;layer.name="Native pigment";layer.stage="base";layer.bounds=object.bounds;
        Command setup;setup.operations={InstallResource{res},AddNode{object},AddNode{layer}};
        if(!doc.value().apply(setup,0))return 3;
        TileData source;source.width=unsigned(w);source.height=unsigned(h);source.rowStride=w*4;source.format=spec.value().colorSpec.format;source.profileId=spec.value().colorSpec.profileId;
        std::ifstream in(argv[1],std::ios::binary);source.bytes.assign(std::istreambuf_iterator<char>(in),{});if(source.bytes.size()!=w*h*4)return 2;
        const auto budget=std::stoul(argv[6]);
        PlanningRegion region{doc.value().snapshot(),object.id,layer.id,object.bounds,mask,&source,314,budget,1000000,{},true};
        const auto batch=std::string(argv[4])=="multiscale"?planMultiscalePigment(region):planBasePigment(region,StyleSettings{"marker",2,1,1,.04});
        if(!batch){std::cerr<<batch.error().messageKey<<'\n';return 3;}
        const auto applied=commitReconstructionBatch(doc.value(),batch.value());if(!applied){std::cerr<<applied.error().messageKey;return 3;}
        source.bytes.clear();const std::filesystem::path output=argv[5];
        if(std::filesystem::exists(output))return 2;
        std::filesystem::create_directories(output);
        ProjectStore store;if(!store.save(output/"project.draw",doc.value().snapshot()))return 3;
        auto reopened=store.recover(output/"project.draw");if(!reopened)return 3;
        const auto snapshot=reopened.value().snapshot;ReferenceRenderer renderer;
        Bytes pixels(w*h*4);
        for(unsigned y=0;y<h;y+=TileSide)for(unsigned x=0;x<w;x+=TileSide){
            Rect box{x,y,std::min<std::uint64_t>(TileSide,w-x),std::min<std::uint64_t>(TileSide,h-y)};
            auto frame=renderer.renderRegion(snapshot,layer.id,box,{snapshot.revision});if(!frame)return 3;
            for(unsigned row=0;row<box.height;++row)std::copy_n(frame.value().bytes.data()+row*frame.value().rowStride,box.width*4,pixels.data()+((y+row)*w+x)*4);
        }
        std::ofstream out(output/"final.rgba",std::ios::binary);out.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));
        std::cout<<"{\"events\":"<<snapshot.eventIndex->size()<<",\"budget\":"<<budget<<",\"sourceFreeReopen\":true,\"sourceReads\":"<<renderer.statistics().sourceAssetReads<<"}\n";
    }catch(const std::exception& e){std::cerr<<e.what();return 3;}
}
