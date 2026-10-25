#include "PaintedDocument.h"
#include "view/LayerTree.h"
#include <algorithm>
#include <cmath>
#include <functional>
namespace drawborad {
namespace {
void over(TileData& destination,const TileData& source,std::uint8_t opacity){
    for(std::size_t at=0;at<source.bytes.size();at+=4){
        const auto sa=(unsigned(source.bytes[at+3])*opacity+127)/255;
        if(!sa)continue;
        const auto da=unsigned(destination.bytes[at+3]);
        const auto den=sa*255+da*(255-sa);
        for(unsigned c=0;c<3;++c){
            const auto num=unsigned(source.bytes[at+c])*sa*255+unsigned(destination.bytes[at+c])*da*(255-sa);
            destination.bytes[at+c]=std::uint8_t((num+den/2)/den);
        }
        destination.bytes[at+3]=std::uint8_t((den+127)/255);
    }
}
}
Result<PaintedDocument> renderPaintedDocument(const DocumentSnapshot& snapshot,EventLimit limit,std::stop_token stop){
    auto valid=validateSnapshot(snapshot);if(!valid)return valid.error();
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"painted.cancelled");
    const auto size=snapshot.spec.pixelSize;
    if(limit.revision!=snapshot.revision)return error(ErrorCode::StaleRevision,"painted.revision");
    if(snapshot.spec.colorSpec.format!=PixelFormat{ColorMode::RGB,Depth::U8,true} ||
       size.width>4096 || size.height>4096 || size.width*size.height>4*1024*1024 ||
       size.width*size.height*4*(snapshot.nodes->size()*2+2)>128*1024*1024)
        return error(ErrorCode::BudgetExceeded,"painted.native-layout-limit");
    for(const auto& [id,node]:*snapshot.nodes){
        if(node.transform!=LayerTransform{} || !nativeCompositingSupported(snapshot,id))
            return error(ErrorCode::UnsupportedFeature,"painted.native-compositing-required");
    }
    TileData empty;empty.width=std::uint32_t(size.width);empty.height=std::uint32_t(size.height);
    empty.format=snapshot.spec.colorSpec.format;empty.profileId=snapshot.spec.colorSpec.profileId;
    empty.rowStride=size.width*4;empty.bytes.resize(std::size_t(empty.rowStride*size.height));empty.engineVersion=2;
    PaintedDocument output;output.merged=empty;ReferenceRenderer renderer;
    if(!snapshot.spec.background.transparent){
        auto paper=empty;const auto color=snapshot.spec.background.paperColor;
        for(std::size_t at=0;at<paper.bytes.size();at+=4){
            for(unsigned c=0;c<3;++c)paper.bytes[at+c]=std::uint8_t(std::lround(color.channels[c]*255));
            paper.bytes[at+3]=255;
        }
        output.merged=paper;PsdLayerPixels layer;layer.name="Paper";layer.pixels=std::move(paper);output.layers.push_back(std::move(layer));
    }
    std::function<Result<void>(NodeId)> visit=[&](NodeId id)->Result<void>{
        if(stop.stop_requested())return error(ErrorCode::Cancelled,"painted.cancelled");
        const auto& node=snapshot.nodes->at(id);
        if(node.kind==NodeKind::Layer){
            if(!nativeLayerSupported(snapshot,id))return error(ErrorCode::UnsupportedFeature,"painted.native-layer-required");
            auto raw=empty;
            for(std::uint64_t y=0;y<size.height;y+=TileSide)for(std::uint64_t x=0;x<size.width;x+=TileSide){
                const Rect rect{std::int64_t(x),std::int64_t(y),std::min<std::uint64_t>(TileSide,size.width-x),std::min<std::uint64_t>(TileSide,size.height-y)};
                auto tile=renderer.renderRegion(snapshot,id,rect,limit,stop);if(!tile)return tile.error();
                for(std::uint32_t row=0;row<tile.value().height;++row)
                    std::copy_n(tile.value().bytes.begin()+std::ptrdiff_t(row*tile.value().rowStride),std::size_t(rect.width*4),raw.bytes.begin()+std::ptrdiff_t((y+row)*raw.rowStride+x*4));
            }
            auto masked=nativeMaskedTile(snapshot,id,raw,0,0);if(!masked)return masked.error();
            PsdLayerPixels layer;layer.name=node.name;layer.opacity=std::uint8_t(std::lround(node.opacity*255));
            layer.visible=node.visible;layer.pixels=std::move(raw);
            auto maskImage=empty;for(std::size_t at=3;at<maskImage.bytes.size();at+=4)maskImage.bytes[at]=255;
            auto mask=nativeMaskedTile(snapshot,id,std::move(maskImage),0,0);if(!mask)return mask.error();
            bool hasMask=false;layer.mask.resize(std::size_t(size.width*size.height));
            for(std::size_t p=0;p<layer.mask.size();++p){layer.mask[p]=mask.value().bytes[p*4+3];hasMask=hasMask || layer.mask[p]!=255;}
            if(hasMask)layer.maskBounds={0,0,size.width,size.height};else layer.mask.clear();
            if(layerVisible(snapshot,id))over(output.merged,masked.value(),layer.opacity);
            output.layers.push_back(std::move(layer));return {};
        }
        const bool group=node.kind!=NodeKind::Root;
        if(group){PsdLayerPixels boundary;boundary.name="</Layer group>";boundary.sectionType=3;boundary.pixels=empty;output.layers.push_back(std::move(boundary));}
        for(const auto child:node.children){auto result=visit(child);if(!result)return result.error();}
        if(group){PsdLayerPixels top;top.name=node.name;top.sectionType=2;top.visible=node.visible;top.pixels=empty;output.layers.push_back(std::move(top));}
        return {};
    };
    auto visited=visit(Document::rootId());if(!visited)return visited.error();
    output.statistics=renderer.statistics();return output;
}
}
