#pragma once
#include "PlanningInternal.h"
#include <array>
#include <unordered_map>

namespace drawborad::planning {
namespace contour_pigment {
using Point=std::pair<int,int>;
struct Edge { int x{},y{},insideX{},insideY{},direction{}; bool visited{}; };
inline std::uint64_t edgeKey(int x,int y,int direction,int width) {
    return (std::uint64_t(y)*std::uint64_t(width+1)+std::uint64_t(x))*4+
           std::uint64_t(direction);
}
inline void appendCollinear(std::vector<Point>& path,Point point) {
    if(!path.empty() && path.back()==point)return;
    if(path.size()>1) {
        const auto [ax,ay]=path[path.size()-2];const auto [bx,by]=path.back();
        const auto dx=std::int64_t(bx)-ax,dy=std::int64_t(by)-ay;
        const auto ex=std::int64_t(point.first)-bx,ey=std::int64_t(point.second)-by;
        // Remove only redundant samples on the same straight segment. No
        // curve shortcuts across concavities, holes, or unreviewed pixels.
        if(dx*ey==dy*ex && dx*ex+dy*ey>0) {path.back()=point;return;}
    }
    path.push_back(point);
}
inline Result<void> emitPath(EventBatch& batch,const PlanningRegion& region,
                            const StyleSettings& style,std::vector<Point>& path,
                            std::uint64_t& samples,std::uint64_t& time) {
    if(path.empty())return {};
    // Start a closed loop at its strongest pigment boundary, so a coherent
    // color run is not split just because tracing began at the top-left cell.
    if(style.maxColorVariation>0 && path.size()>1) {
        double strongest=0;std::size_t start=0;
        auto previous=colorAt(region,path.back().first,path.back().second);
        for(std::size_t i=0;i<path.size();++i) {
            const auto color=colorAt(region,path[i].first,path[i].second);
            double change=std::abs(color.alpha-previous.alpha);
            for(unsigned c=0;c<color.activeChannels;++c)
                change=std::max(change,std::abs(color.channels[c]-previous.channels[c]));
            if(change>strongest) {strongest=change;start=i;}
            previous=color;
        }
        std::rotate(path.begin(),path.begin()+std::ptrdiff_t(start),path.end());
    }
    StrokeStyle brush;brush.width=style.width;brush.opacity=style.opacity;brush.hardness=1;
    std::size_t first=0;
    while(first<path.size()) {
        if(region.stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
        auto color=colorAt(region,path[first].first,path[first].second);
        std::array<double,5> sum{},low{},high{};
        const auto channels=color.activeChannels;
        for(unsigned c=0;c<=channels;++c)sum[c]=low[c]=high[c]=c==channels?color.alpha:color.channels[c];
        std::size_t last=first+1;
        for(;last<path.size();++last) {
            if((last&1023)==0 && region.stop.stop_requested())
                return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
            const auto next=colorAt(region,path[last].first,path[last].second);
            bool coherent=true;
            for(unsigned c=0;c<=channels;++c) {
                const auto value=c==channels?next.alpha:next.channels[c];
                const auto mean=(sum[c]+value)/double(last-first+1);
                if(style.maxColorVariation>0 &&
                   std::max(std::max(high[c],value)-mean,mean-std::min(low[c],value))>
                       style.maxColorVariation)coherent=false;
            }
            if(!coherent)break;
            for(unsigned c=0;c<=channels;++c) {
                const auto value=c==channels?next.alpha:next.channels[c];
                sum[c]+=value;low[c]=std::min(low[c],value);high[c]=std::max(high[c],value);
            }
        }
        for(unsigned c=0;c<channels;++c)color.channels[c]=sum[c]/double(last-first);
        color.alpha=sum[channels]/double(last-first);
        std::vector<Point> compact;
        for(auto i=first;i<last;++i)appendCollinear(compact,path[i]);
        // Close uniform loops; split pigment runs already meet at adjacent
        // centers. Repeating the first point after a color boundary would
        // otherwise create an unnecessary separate dab at the seam.
        if(first==0 && last==path.size())appendCollinear(compact,path.front());
        if(compact.size()==1)compact.push_back(compact.front()); // A real isolated nib dab.
        auto added=addEvent(batch,region,brush,style.toolId,color,compact,2,samples,time);
        if(!added)return added.error();
        first=last;
    }
    return {};
}
}

// For native persisted masks, fill the reviewed form with nested connected
// pigment bands. Every result is an ordinary replayable brush event. The
// distance raster is temporary planning data, never persisted or rendered.
inline Result<EventBatch> planContourPigment(const PlanningRegion& region,
                                            const StyleSettings& style,
                                            const SafeRaster& safe) {
    using namespace contour_pigment;
    const int w=safe.width,h=safe.height;
    const auto offset=[&](int x,int y){return std::size_t(y)*std::size_t(w)+std::size_t(x);};
    std::vector<int> depth(safe.pixels.size(),0);
    const auto distance=[&](int x,int y){return x<0 || y<0 || x>=w || y>=h?0:depth[offset(x,y)];};
    // Exact 8-neighbor distance to the outside, computed in two linear passes.
    // Chebyshev levels preserve narrow components and never bridge mask holes.
    for(int y=0;y<h;++y) {
        if((y&63)==0 && region.stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
        for(int x=0;x<w;++x)if(safe.pixels[offset(x,y)])
            depth[offset(x,y)]=1+std::min({distance(x-1,y),distance(x-1,y-1),distance(x,y-1),distance(x+1,y-1)});
    }
    int deepest=0;
    for(int y=h-1;y>=0;--y) {
        if((y&63)==0 && region.stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
        for(int x=w-1;x>=0;--x)if(depth[offset(x,y)]) {
            auto& d=depth[offset(x,y)];
            d=std::min(d,1+std::min({distance(x+1,y),distance(x+1,y+1),distance(x,y+1),distance(x-1,y+1)}));
            deepest=std::max(deepest,d);
        }
    }
    if(!deepest)return error(ErrorCode::InvalidInput,"reconstruct.empty-safe-region");
    // Bound spacing by the opaque nib core, including the diagonal distance
    // between levels. This covers centers and one-pixel remnants without a
    // separate scanline/pixel residual pass.
    const int coreStep=1+int(std::floor(std::max(0.0,style.width*.5-.5)/std::sqrt(2.0)));
    const int step=std::min(std::max(1,int(std::lround(style.spacing))),coreStep);
    std::vector<std::vector<std::size_t>> bands(std::size_t((deepest-1)/step+1));
    for(std::size_t p=0;p<depth.size();++p) {
        if((p&16383)==0 && region.stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
        if(depth[p] && (depth[p]-1)%step==0)bands[std::size_t((depth[p]-1)/step)].push_back(p);
    }
    EventBatch batch{region.snapshot.revision,region.objectId,region.layerId,{}};
    std::uint64_t samples=0,time=100;
    constexpr int dx[4]={1,0,-1,0},dy[4]={0,1,0,-1};
    for(std::size_t band=0;band<bands.size();++band) {
        if(region.stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
        const int level=1+int(band)*step;
        std::vector<Edge> edges;
        std::unordered_map<std::uint64_t,std::size_t> outgoing;
        const auto add=[&](int x,int y,int ix,int iy,int d)->Result<void> {
            // Bound the transient graph too, even if later collinear reduction
            // would fit the event sample budget.
            if(edges.size()>=region.maxSamples*8ull)return error(ErrorCode::BudgetExceeded,"reconstruct.contour-complexity");
            outgoing.emplace(edgeKey(x,y,d,w),edges.size());edges.push_back({x,y,ix,iy,d,false});return {};
        };
        for(const auto p:bands[band]) {
            if((p&1023)==0 && region.stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
            const int x=int(p%std::size_t(w)),y=int(p/std::size_t(w));
            Result<void> added;
            if(distance(x,y-1)<level)added=add(x,y,x,y,0);
            if(added && distance(x+1,y)<level)added=add(x+1,y,x,y,1);
            if(added && distance(x,y+1)<level)added=add(x+1,y+1,x,y,2);
            if(added && distance(x-1,y)<level)added=add(x,y+1,x,y,3);
            if(!added)return added.error();
        }
        for(std::size_t start=0;start<edges.size();++start) {
            if(edges[start].visited)continue;
            std::vector<Point> path;
            const auto append=[&](Point p) {
                if(!path.empty()) {
                    const auto previous=path.back();
                    if(previous==p)return;
                    // At a concave corner the edge walk jumps diagonally
                    // between pixel centers. Visit the interior corner cell
                    // too: a two-pixel nib otherwise leaves it translucent.
                    if(std::abs(previous.first-p.first)==1 && std::abs(previous.second-p.second)==1) {
                        const Point a{previous.first,p.second},b{p.first,previous.second};
                        if(distance(a.first-safe.left,a.second-safe.top)==level)path.push_back(a);
                        else if(distance(b.first-safe.left,b.second-safe.top)==level)path.push_back(b);
                    }
                }
                path.push_back(p);
            };
            auto current=start;bool closed=false;
            for(std::size_t walked=0;walked<edges.size();++walked) {
                if((walked&1023)==0 && region.stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
                auto& e=edges[current];if(e.visited)break;e.visited=true;
                const Point p{e.insideX+safe.left,e.insideY+safe.top};
                append(p);
                const int x=e.x+dx[e.direction],y=e.y+dy[e.direction];
                if(x==edges[start].x && y==edges[start].y) {closed=true;break;}
                auto next=edges.size();
                for(const int turn:{1,0,3,2}) {
                    const auto it=outgoing.find(edgeKey(x,y,(e.direction+turn)%4,w));
                    if(it!=outgoing.end() && !edges[it->second].visited) {next=it->second;break;}
                }
                if(next==edges.size())break;
                current=next;
            }
            if(!closed)return error(ErrorCode::CorruptData,"reconstruct.contour-open");
            if(!path.empty())append(path.front());
            if(path.size()>1 && path.front()==path.back())path.pop_back();
            auto emitted=emitPath(batch,region,style,path,samples,time);if(!emitted)return emitted.error();
        }
    }
    if(batch.events.empty())return error(ErrorCode::InvalidInput,"reconstruct.empty-safe-region");
    return validateReconstructionBatch(batch);
}
} // namespace drawborad::planning
