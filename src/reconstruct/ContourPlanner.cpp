#include "PlanningInternal.h"
#include <array>
#include <unordered_map>

namespace drawborad {
namespace {
struct Edge {
    int x{},y{},insideX{},insideY{},direction{};
    bool visited{};
};
std::uint64_t key(int x,int y,int dir,int stride){
    return (std::uint64_t(y)*std::uint64_t(stride)+std::uint64_t(x))*4ull+
           std::uint64_t(dir);
}
}
Result<EventBatch> planContours(const PlanningRegion& region) {
    const auto validated=planning::checkRegion(region,false);
    if(!validated)return validated.error();
    auto raster=planning::makeSafe(region,1.0);
    if(!raster)return raster.error();
    const auto& safe=raster.value();
    std::vector<Edge> edges;
    std::unordered_map<std::uint64_t,std::size_t> outgoing;
    const int dx[4]={1,0,-1,0},dy[4]={0,1,0,-1};
    const auto add=[&](int vx,int vy,int ix,int iy,int d)->Result<void>{
        if(edges.size()>=region.maxSamples*2ull)
            return error(ErrorCode::BudgetExceeded,"reconstruct.contour-complexity");
        outgoing.emplace(key(vx,vy,d,safe.width+1),edges.size());
        edges.push_back({vx,vy,ix,iy,d,false});
        return {};
    };
    for(int y=safe.top;y<safe.top+safe.height;++y){
        if((y&127)==0 && region.stop.stop_requested())
            return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
        for(int x=safe.left;x<safe.left+safe.width;++x){
            if(!safe.at(x,y))continue;
            Result<void> added;
            if(!safe.at(x,y-1))added=add(x-safe.left,y-safe.top,x,y,0);
            if(added && !safe.at(x+1,y))added=add(x+1-safe.left,y-safe.top,x,y,1);
            if(added && !safe.at(x,y+1))added=add(x+1-safe.left,y+1-safe.top,x,y,2);
            if(added && !safe.at(x-1,y))added=add(x-safe.left,y+1-safe.top,x,y,3);
            if(!added)return added.error();
        }
    }
    if(edges.empty())return error(ErrorCode::InvalidInput,"reconstruct.empty-safe-region");
    EventBatch result{region.snapshot.revision,region.objectId,region.layerId,{}};
    StrokeStyle style;style.width=1;style.opacity=.8;style.hardness=1;
    std::uint64_t samples=0,time=100;
    for(std::size_t index=0;index<edges.size();++index){
        if(edges[index].visited)continue;
        if(region.stop.stop_requested())
            return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
        std::vector<std::pair<int,int>> path;
        std::size_t current=index;
        bool closed=false;
        for(std::size_t count=0;count<=edges.size();++count){
            auto& edge=edges[current];
            if(edge.visited)break;
            edge.visited=true;
            const std::pair<int,int> point{edge.insideX,edge.insideY};
            if(path.empty() || path.back()!=point)path.push_back(point);
            const auto ex=edge.x+dx[edge.direction],ey=edge.y+dy[edge.direction];
            if(ex==edges[index].x && ey==edges[index].y){closed=true;break;}
            // Prefer a right turn. Keeps diagonally touching components separate.
            constexpr std::array<int,4> turn{1,0,3,2};
            std::size_t next=edges.size();
            for(const auto delta:turn){
                const int direction=(edge.direction+delta)%4;
                const auto it=outgoing.find(key(ex,ey,direction,safe.width+1));
                if(it!=outgoing.end() && !edges[it->second].visited){
                    next=it->second;break;
                }
            }
            if(next==edges.size())break;
            current=next;
        }
        if(!closed)return error(ErrorCode::CorruptData,"reconstruct.contour-open");
        if(path.size()<3)continue;
        path.push_back(path.front());
        auto added=planning::addEvent(result,region,style,"guide",
                                      planning::contourColor(region),path,1,samples,time);
        if(!added)return added.error();
    }
    if(result.events.empty())return error(ErrorCode::InvalidInput,"reconstruct.empty-safe-region");
    return validateReconstructionBatch(result);
}
} // namespace drawborad
