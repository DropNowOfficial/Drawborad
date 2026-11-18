#include "PlanningInternal.h"
#include "render/CoverageRasterizer.h"

namespace drawborad {
namespace {
struct Point {double x,y;};
// Local RGB structure tensor: chromatic boundaries matter even at equal luminance.
Point tangent(const TileData& src,Point p,double width) {
    double xx=0,xy=0,yy=0;
    const int radius=std::max(1,int(width*.5));
    const auto sample=[&](int x,int y,int c){return planning::sampleChannel(src,
        std::clamp(x,0,int(src.width)-1),std::clamp(y,0,int(src.height)-1),c);};
    for(int j=-1;j<=1;++j)for(int i=-1;i<=1;++i)for(int c=0;c<3;++c){
        const int x=int(p.x)+i*radius,y=int(p.y)+j*radius;
        const double gx=sample(x+radius,y,c)-sample(x-radius,y,c);
        const double gy=sample(x,y+radius,c)-sample(x,y-radius,c);
        xx+=gx*gx;xy+=gx*gy;yy+=gy*gy;
    }
    const double angle=.5*std::atan2(2*xy,xx-yy)+1.5707963267948966;
    return {std::cos(angle),std::sin(angle)};
}
}
Result<EventBatch> planMultiscalePigment(const PlanningRegion& region){
    const auto checked=planning::checkRegion(region,true);if(!checked)return checked.error();
    if(!region.clipToReviewedMask || region.source->format!=PixelFormat{ColorMode::RGB,Depth::U8,true})
        return error(ErrorCode::UnsupportedFeature,"reconstruct.multiscale-layout");
    // Allocate planning feedback buffers strictly over the object's active ROI
    // bounding box rather than full canvas, enabling up to 20M-pixel documents
    // within the 128 MiB budget.
    if(region.bounds.width*region.bounds.height>20000000ull)
        return error(ErrorCode::BudgetExceeded,"reconstruct.multiscale-area");
    const auto& src=*region.source;const int w=int(src.width),h=int(src.height);
    const auto inside=[&](Point p){return p.x>=0 && p.y>=0 && p.x<w && p.y<h &&
        region.reviewedMask[std::size_t(int(p.y))*w+std::size_t(int(p.x))]==255;};
    int roiX0=w,roiY0=h,roiX1=0,roiY1=0;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
        if(region.reviewedMask[std::size_t(y)*w+x]){
            if(src.bytes[std::size_t(y)*src.rowStride+x*4+3]!=255)
                return error(ErrorCode::UnsupportedFeature,"reconstruct.multiscale-soft-alpha");
            roiX0=std::min(roiX0,x);roiY0=std::min(roiY0,y);
            roiX1=std::max(roiX1,x+1);roiY1=std::max(roiY1,y+1);
        }
    if(roiX0>=roiX1 || roiY0>=roiY1)
        return error(ErrorCode::InvalidInput,"reconstruct.empty-safe-region");
    roiX0=std::max(0,roiX0-16);roiY0=std::max(0,roiY0-16);
    roiX1=std::min(w,roiX1+16);roiY1=std::min(h,roiY1+16);
    const int roiW=roiX1-roiX0,roiH=roiY1-roiY0;
    // Broad underpainting remains ordinary connected brush geometry. Corrections
    // share its native layer, so feedback uses exactly the replay blend operation.
    auto underpainting=region;
    underpainting.maxEvents=std::max<std::uint64_t>(1,region.maxEvents/3);
    underpainting.maxSamples=std::max<std::uint64_t>(2,region.maxSamples/3);
    auto base=planBasePigment(underpainting,StyleSettings{"marker",4,2,1,.06});
    // Reserve detail capacity before planning; widening a nib never resizes the
    // source or silently raises the caller's event/sample budget.
    for(const double width:{8.,12.,16.,24.}){
        if(base || base.error().code!=ErrorCode::BudgetExceeded)break;
        base=planBasePigment(underpainting,StyleSettings{"marker",width,width*.5,1,.06});
    }
    if(!base)return base.error();
    auto batch=std::move(base.value());
    auto snapshot=region.snapshot;snapshot.eventIndex=std::make_shared<const EventList>(batch.events);
    ReferenceRenderer renderer;
    TileData canvas;canvas.width=std::uint32_t(roiW);canvas.height=std::uint32_t(roiH);canvas.rowStride=std::uint64_t(roiW)*4;
    canvas.format=src.format;canvas.profileId=src.profileId;canvas.bytes.resize(std::size_t(canvas.rowStride)*std::size_t(roiH));
    for(int y=roiY0;y<roiY1;y+=int(TileSide))for(int x=roiX0;x<roiX1;x+=int(TileSide)){
        Rect box{x,y,std::uint64_t(std::min(int(TileSide),roiX1-x)),std::uint64_t(std::min(int(TileSide),roiY1-y))};
        auto rendered=renderer.renderRegion(snapshot,region.layerId,box,{snapshot.revision},region.stop);
        if(!rendered)return rendered.error();
        auto clipped=nativeMaskedTile(snapshot,region.layerId,rendered.value(),x,y);if(!clipped)return clipped.error();
        const auto& tile=clipped.value();
        for(unsigned row=0;row<tile.height;++row)std::copy_n(tile.bytes.data()+row*tile.rowStride,tile.width*4,
            canvas.bytes.data()+std::size_t(int(row)+y-roiY0)*canvas.rowStride+std::size_t(x-roiX0)*4);
    }
    std::uint64_t samples=0,time=100;for(const auto& e:batch.events){samples+=e.geometry.size();time=std::max(time,e.endUs+100);}
    const auto initial=batch.events.size(),available=region.maxEvents-initial;
    const auto squaredError=[&](int x,int y){
        double value=0;
        const auto a=std::size_t(y-roiY0)*canvas.rowStride+std::size_t(x-roiX0)*4;
        const auto b=std::size_t(y)*src.rowStride+std::size_t(x)*4;
        for(int c=0;c<3;++c){const auto d=double(canvas.bytes[a+c])-src.bytes[b+c];value+=d*d;}
        return value;
    };
    struct Seed {double score;int x,y;};
    std::uint64_t trials=0;
    for(const double width:{8.,4.,2.,1.2}){
        const double fraction=width==8?.10:width==4?.30:width==2?.60:1.;
        const auto limit=initial+std::uint64_t(double(available)*fraction);
        const int cell=std::max(2,int(width*1.5));
        for(int pass=0;pass<2;++pass){
            std::vector<Seed> seeds;
            for(int y=roiY0;y<roiY1;y+=cell){
              if(region.stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
              for(int x=roiX0;x<roiX1;x+=cell){
                Seed best{36,0,0};
                for(int yy=y;yy<std::min(y+cell,roiY1);++yy)for(int xx=x;xx<std::min(x+cell,roiX1);++xx)
                    if(inside({xx+.5,yy+.5})){
                        const auto score=squaredError(xx,yy);if(score>best.score)best={score,xx,yy};
                    }
                if(best.score>36)seeds.push_back(best);
              }
            }
            std::stable_sort(seeds.begin(),seeds.end(),[](const auto& a,const auto& b){return a.score>b.score;});
            for(const auto& seed:seeds){
                if(region.stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
                if(batch.events.size()>=limit)break;
                if(++trials>200000)return error(ErrorCode::BudgetExceeded,"reconstruct.multiscale-work");
                if(squaredError(seed.x,seed.y)<36)continue;
                const Point center{seed.x+.5,seed.y+.5};const auto direction=tangent(src,center,width);
                std::vector<Point> halves[2];
                const auto origin=planning::colorAt(region,seed.x,seed.y);
                for(int side=0;side<2;++side){
                    Point p=center,previous{direction.x*(side?1:-1),direction.y*(side?1:-1)};
                    const double step=.75;
                    for(int k=0;k<std::max(3,int(width*2));++k){
                        auto d=tangent(src,p,width);if(d.x*previous.x+d.y*previous.y<0){d.x=-d.x;d.y=-d.y;}
                        // Smooth sign-consistent changes, never bridge a mask gap.
                        d={.7*d.x+.3*previous.x,.7*d.y+.3*previous.y};const auto length=std::hypot(d.x,d.y);
                        if(length<.1)break;
                        d={d.x/length,d.y/length};Point next{p.x+d.x*step,p.y+d.y*step};
                        if(!inside(next) || !inside({(p.x+next.x)*.5,(p.y+next.y)*.5}))break;
                        const auto color=planning::colorAt(region,int(next.x),int(next.y));double delta=0;
                        for(int c=0;c<3;++c)delta=std::max(delta,std::abs(color.channels[c]-origin.channels[c]));
                        if(delta>.14)break;
                        halves[side].push_back(next);p=next;previous=d;
                    }
                }
                if(halves[0].size()+halves[1].size()<4)continue;
                StrokeEvent event;event.id=planning::eventId(region,4,batch.events.size());
                event.objectId=region.objectId;event.layerId=region.layerId;event.parentRevision=region.snapshot.revision;
                event.engineVersion=2;event.seed=region.seed;event.toolId="marker";event.color=origin;
                event.style.width=width;event.style.hardness=1;event.style.opacity=1;event.startUs=time;
                const auto append=[&](Point p){event.geometry.push_back({p.x,p.y,1,{},{},time+event.geometry.size()*100,0,0});};
                for(auto i=halves[0].rbegin();i!=halves[0].rend();++i)append(*i);
                append(center);
                for(const auto& p:halves[1])append(p);
                event.endUs=event.geometry.back().timestampUs;
                if(event.geometry.size()>region.maxSamples-samples)break;
                auto bounds=strokeBounds(event);if(!bounds)return bounds.error();
                const auto b=bounds.value();const auto x0=std::max<std::int64_t>(roiX0,b.x),y0=std::max<std::int64_t>(roiY0,b.y);
                const auto x1=std::min<std::int64_t>(roiX1,b.x+std::int64_t(b.width)),y1=std::min<std::int64_t>(roiY1,b.y+std::int64_t(b.height));
                if(x0>=x1 || y0>=y1 || std::uint64_t(x1-x0)>TileSide || std::uint64_t(y1-y0)>TileSide)continue;
                Rect box{x0,y0,std::uint64_t(x1-x0),std::uint64_t(y1-y0)};
                auto coverage=strokeCoverage(event,box,UINT64_MAX,region.stop);if(!coverage)return coverage.error();
                TileData patch;patch.width=std::uint32_t(box.width);patch.height=std::uint32_t(box.height);patch.rowStride=box.width*4;
                patch.format=src.format;patch.profileId=src.profileId;patch.bytes.resize(std::size_t(patch.rowStride)*patch.height);
                std::array<double,3> numerator{};double denominator=0;
                for(unsigned y=0;y<patch.height;++y)for(unsigned x=0;x<patch.width;++x){
                    const auto ci=std::size_t(int(y)+int(y0)-roiY0)*canvas.rowStride+std::size_t(int(x)+int(x0)-roiX0)*4;
                    const auto si=std::size_t(y+y0)*src.rowStride+(x+x0)*4,pi=std::size_t(y)*patch.rowStride+x*4;
                    std::copy_n(canvas.bytes.data()+ci,4,patch.bytes.data()+pi);
                    auto& cov=coverage.value()[std::size_t(y)*patch.width+x];if(!region.reviewedMask[std::size_t(y+y0)*w+x+x0])cov=0;
                    const double a=cov/65535.;denominator+=a*a;
                    for(int c=0;c<3;++c)numerator[c]+=a*(src.bytes[si+c]-canvas.bytes[ci+c]*(1-a));
                }
                if(denominator<1e-8)continue;
                for(int c=0;c<3;++c)event.color.channels[c]=std::clamp(numerator[c]/denominator/255.,0.,1.);
                blendCoverage(patch,event,coverage.value());double gain=0;
                for(unsigned y=0;y<patch.height;++y)for(unsigned x=0;x<patch.width;++x){
                    if(!region.reviewedMask[std::size_t(y+y0)*w+x+x0])continue;
                    const auto si=std::size_t(y+y0)*src.rowStride+(x+x0)*4,pi=std::size_t(y)*patch.rowStride+x*4;
                    gain+=squaredError(int(x+x0),int(y+y0));
                    for(int c=0;c<3;++c){const auto d=double(patch.bytes[pi+c])-src.bytes[si+c];gain-=d*d;}
                }
                if(gain<48)continue;
                for(unsigned y=0;y<patch.height;++y)std::copy_n(patch.bytes.data()+y*patch.rowStride,patch.rowStride,
                    canvas.bytes.data()+std::size_t(int(y)+int(y0)-roiY0)*canvas.rowStride+std::size_t(int(x0)-roiX0)*4);
                samples+=event.geometry.size();time=event.endUs+100;batch.events.push_back(std::move(event));
            }
        }
    }
    return validateReconstructionBatch(batch);
}
} // namespace drawborad
