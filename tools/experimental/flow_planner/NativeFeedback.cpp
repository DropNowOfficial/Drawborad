// Research-only in-memory error feedback. Actual Drawborad coverage and blend,
// no reference raster resource enters a document or replay stream.
#include "render/CoverageRasterizer.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <limits>
using namespace drawborad;
namespace {
struct Canvas {
    unsigned w,h; double gradientWeight=0; bool candidateReady=false,pendingReady=false; Bytes target,under,active,output,pending; Rect rect;
    StrokeEvent event; std::vector<std::uint16_t> coverage;
    Canvas(unsigned width,unsigned height,const std::uint8_t* source):w(width),h(height),target(source,source+std::size_t(w)*h*4),under(std::size_t(w)*h*4),active(under),output(under){}
};
void over(std::uint8_t* dest,const std::uint8_t* source) {
    const unsigned sa=source[3],da=dest[3],den=sa*255+da*(255-sa);
    if(!den)return;
    for(unsigned c=0;c<3;++c)dest[c]=std::uint8_t((unsigned(source[c])*sa*255+unsigned(dest[c])*da*(255-sa)+den/2)/den);
    dest[3]=std::uint8_t((den+127)/255);
}
double loss(const std::uint8_t* p,const std::uint8_t* t) {
    double sum=0;
    for(unsigned c=0;c<3;++c) {
        const auto black=(double(p[c])*p[3]-double(t[c])*t[3])/65025.;
        const auto white=black+(double(t[3])-p[3])/255.;
        sum+=.5*(black*black+white*white);
    }
    const auto alpha=(double(p[3])-t[3])/255.;return sum+alpha*alpha;
}
}
extern "C" {
void* flow_create(unsigned w,unsigned h,const std::uint8_t* source,std::size_t sourceBytes) {
    try {if(!w || !h || w>512 || h>512 || !source || sourceBytes!=std::size_t(w)*h*4)return nullptr;return new Canvas(w,h,source);}catch(...){return nullptr;}
}
void flow_destroy(void* p){delete static_cast<Canvas*>(p);}
int flow_gradient_weight(void* p,double weight){
    if(!p)return -1;
    auto& c=*static_cast<Canvas*>(p);c.pendingReady=false;
    if(!std::isfinite(weight)||weight<0||weight>2)return -1;
    c.gradientWeight=weight;return 0;
}
int flow_phase(void* p){
    if(!p)return -1;
    auto& c=*static_cast<Canvas*>(p);c.under=c.output;std::fill(c.active.begin(),c.active.end(),0);c.candidateReady=false;c.pendingReady=false;return 0;
}
std::uint8_t* flow_pixels(void* p){return p?static_cast<Canvas*>(p)->output.data():nullptr;}
int flow_candidate(void* p,double width,double hardness,unsigned n,const double* points,std::size_t pointValues,const std::uint8_t* mask,std::size_t maskBytes,int* bounds) {
    try {
        if(!p)return -1;
        auto& c=*static_cast<Canvas*>(p);c.candidateReady=false;c.pendingReady=false;
        if(!n || n>500000 || pointValues!=std::size_t(n)*3 || maskBytes!=std::size_t(c.w)*c.h || !std::isfinite(width) || width<=0 || width>128 || !std::isfinite(hardness) || hardness<0 || hardness>1 || !mask || !points || !bounds)return -1;
        if(!std::all_of(mask,mask+maskBytes,[](auto value){return value==0||value==255;}))return -1;
        std::size_t work=0;
        for(unsigned i=0;i<n;++i) {
            const auto x=points[i*3],y=points[i*3+1],pressure=points[i*3+2];
            if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(pressure)||x<0||y<0||x>=c.w||y>=c.h||pressure<0||pressure>1||!mask[std::size_t(y)*c.w+std::size_t(x)])return -1;
            if(i) {
                const auto ax=points[(i-1)*3],ay=points[(i-1)*3+1];
                const auto checks=std::max(1u,unsigned(std::ceil(std::hypot(x-ax,y-ay)*4)));
                work+=checks;if(work>2000000)return -1;
                for(unsigned k=1;k<=checks;++k) {
                    const auto t=double(k)/checks,px=ax+(x-ax)*t,py=ay+(y-ay)*t;
                    if(!mask[std::size_t(py)*c.w+std::size_t(px)])return -1;
                }
            }
        }
        StrokeEvent e;e.engineVersion=2;e.toolId="marker";e.style.width=width;e.style.hardness=hardness;e.style.opacity=1;e.color.activeChannels=3;
        for(unsigned i=0;i<n;++i)e.geometry.push_back({points[i*3],points[i*3+1],points[i*3+2],{},{},i,0,0});
        auto box=strokeBounds(e);if(!box)return -2;
        auto r=box.value();auto x0=std::max(std::int64_t(0),r.x),y0=std::max(std::int64_t(0),r.y);
        auto x1=std::min(std::int64_t(c.w),r.x+std::int64_t(r.width)),y1=std::min(std::int64_t(c.h),r.y+std::int64_t(r.height));
        if(x1<=x0||y1<=y0)return -3;
        r={x0,y0,std::uint64_t(x1-x0),std::uint64_t(y1-y0)};
        auto coverage=strokeCoverage(e,r,UINT64_MAX);if(!coverage)return -4;
        c.coverage=std::move(coverage.value());c.rect=r;c.event=std::move(e);
        for(unsigned y=0;y<r.height;++y)for(unsigned x=0;x<r.width;++x)
            if(!mask[(y+unsigned(r.y))*c.w+x+unsigned(r.x)])c.coverage[y*r.width+x]=0;
        bounds[0]=int(x0);bounds[1]=int(y0);bounds[2]=int(r.width);bounds[3]=int(r.height);c.candidateReady=true;return 0;
    } catch(...){return -5;}
}
std::uint16_t* flow_coverage(void* p){return p&&static_cast<Canvas*>(p)->candidateReady?static_cast<Canvas*>(p)->coverage.data():nullptr;}
double flow_evaluate(void* p,const double* color,std::size_t channels,double opacity) {
    try {
        if(!p)return std::numeric_limits<double>::quiet_NaN();
        auto& c=*static_cast<Canvas*>(p);c.pendingReady=false;
        if(!c.candidateReady||!color||channels!=4||!std::isfinite(opacity)||opacity<0||opacity>1)return std::numeric_limits<double>::quiet_NaN();
        for(unsigned channel=0;channel<4;++channel)if(!std::isfinite(color[channel])||color[channel]<0||color[channel]>1)return std::numeric_limits<double>::quiet_NaN();
        auto e=c.event;e.style.opacity=opacity;e.color.channels={color[0],color[1],color[2],0};e.color.alpha=color[3];
        if(!validateRasterEvent(e))return -1e30;
        TileData tile;tile.width=unsigned(c.rect.width);tile.height=unsigned(c.rect.height);tile.rowStride=std::uint64_t(tile.width)*4;
        tile.bytes.resize(tile.rowStride*tile.height);
        for(unsigned y=0;y<tile.height;++y)for(unsigned x=0;x<tile.width;++x)
            std::copy_n(c.active.data()+((y+unsigned(c.rect.y))*c.w+x+unsigned(c.rect.x))*4,4,tile.bytes.data()+(y*tile.width+x)*4);
        blendCoverage(tile,e,c.coverage);double gain=0;Bytes proposed(tile.bytes.size());
        for(unsigned y=0;y<tile.height;++y)for(unsigned x=0;x<tile.width;++x) {
            const auto offset=((y+unsigned(c.rect.y))*c.w+x+unsigned(c.rect.x))*4;std::uint8_t combined[4];
            std::copy_n(c.under.data()+offset,4,combined);over(combined,tile.bytes.data()+(y*tile.width+x)*4);
            std::copy_n(combined,4,proposed.data()+(y*tile.width+x)*4);
            const auto targetAlpha=unsigned(c.target[offset+3]);
            const auto allowed=std::max(unsigned(c.output[offset+3]),std::min(255u,targetAlpha+(targetAlpha?1u:0u)));
            if(combined[3]>allowed)return -1e30;
            gain+=loss(c.output.data()+offset,c.target.data()+offset)-loss(combined,c.target.data()+offset);
        }
        if(c.gradientWeight>0) {
            const auto pixel=[&](int x,int y,bool after)->const std::uint8_t* {
                if(after && x>=c.rect.x && y>=c.rect.y && x<int(c.rect.x+c.rect.width) && y<int(c.rect.y+c.rect.height))
                    return proposed.data()+((unsigned(y-c.rect.y))*tile.width+unsigned(x-c.rect.x))*4;
                return c.output.data()+(unsigned(y)*c.w+unsigned(x))*4;
            };
            const auto edgeLoss=[&](int x,int y,int nx,int ny,bool after) {
                const auto a=pixel(x,y,after),b=pixel(nx,ny,after);
                const auto t=c.target.data()+(unsigned(y)*c.w+unsigned(x))*4,u=c.target.data()+(unsigned(ny)*c.w+unsigned(nx))*4;
                double value=0;
                for(unsigned channel=0;channel<3;++channel) {
                    const auto residual=(double(a[channel])*a[3]-double(b[channel])*b[3]-double(t[channel])*t[3]+double(u[channel])*u[3])/65025.;
                    value+=residual*residual;
                }
                return value;
            };
            constexpr int directions[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
            for(unsigned y=0;y<tile.height;++y)for(unsigned x=0;x<tile.width;++x) {
                const auto px=int(x)+int(c.rect.x),py=int(y)+int(c.rect.y);
                for(const auto& direction:directions) {
                    const auto nx=px+direction[0],ny=py+direction[1];
                    if(nx<0||ny<0||nx>=int(c.w)||ny>=int(c.h))continue;
                    gain+=.5*c.gradientWeight*(edgeLoss(px,py,nx,ny,false)-edgeLoss(px,py,nx,ny,true));
                }
            }
        }
        c.pending=std::move(tile.bytes);c.pendingReady=true;return gain;
    }catch(...){return -1e30;}
}
int flow_commit(void* p) {
    if(!p)return -1;
    auto& c=*static_cast<Canvas*>(p);
    if(!c.candidateReady||!c.pendingReady)return -1;
    for(unsigned y=0;y<c.rect.height;++y)for(unsigned x=0;x<c.rect.width;++x) {
        const auto offset=((y+unsigned(c.rect.y))*c.w+x+unsigned(c.rect.x))*4;
        const auto at=(y*c.rect.width+x)*4;
        std::copy_n(c.pending.data()+at,4,c.active.data()+offset);
        std::copy_n(c.under.data()+offset,4,c.output.data()+offset);over(c.output.data()+offset,c.active.data()+offset);
    }
    c.pendingReady=false;c.candidateReady=false;return 0;
}
}
