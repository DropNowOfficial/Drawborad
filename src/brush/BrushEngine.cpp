#include "BrushEngine.h"
#include <cmath>
namespace drawborad { namespace {
std::uint64_t mix(std::uint64_t x){x+=0x9e3779b97f4a7c15ull;x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;x=(x^(x>>27))*0x94d049bb133111ebull;return x^(x>>31);} }
Result<StrokeEvent> BrushEngine::compile(std::span<const InputSample> in,const BrushSettings& b,bool eraser) const{
 if(in.empty()||in.size()>1000000||b.layerId.empty()||b.objectId.empty()||!std::isfinite(b.width)||b.width<=0||b.width>8192||!std::isfinite(b.spacing)||b.spacing<=0||b.spacing>4)return error(ErrorCode::InvalidInput,"brush.settings");
 std::vector<InputSample> out;out.reserve(in.size()*2);out.push_back(in.front());const double step=std::max(.25,b.width*b.spacing);
 for(std::size_t i=1;i<in.size();++i){auto a=out.back(),z=in[i];if(!std::isfinite(z.x)||!std::isfinite(z.y)||!std::isfinite(z.pressure)||z.pressure<0||z.pressure>1||z.timestampUs<in[i-1].timestampUs)return error(ErrorCode::InvalidInput,"brush.sample");double dx=z.x-a.x,dy=z.y-a.y,d=std::hypot(dx,dy);auto n=std::max<std::uint64_t>(1,std::uint64_t(std::ceil(d/step)));if(out.size()+n>1000000)return error(ErrorCode::BudgetExceeded,"brush.samples");for(std::uint64_t j=1;j<=n;j++){double t=double(j)/double(n);InputSample q=z;q.x=a.x+dx*t;q.y=a.y+dy*t;q.pressure=a.pressure+(z.pressure-a.pressure)*t;q.timestampUs=a.timestampUs+std::uint64_t((z.timestampUs-a.timestampUs)*t);out.push_back(q);}}
 StrokeEvent e;e.id=Id::fromNumber(mix(b.seed^out.front().timestampUs));e.objectId=b.objectId;e.layerId=b.layerId;e.startUs=out.front().timestampUs;e.endUs=out.back().timestampUs;e.toolId=eraser?"eraser":b.toolId;e.color=b.color;e.geometry=std::move(out);e.seed=b.seed;e.parentRevision=b.parentRevision;e.engineVersion=2;e.style.width=b.width;e.style.opacity=b.opacity;e.style.hardness=b.hardness;e.style.shape=b.shape;e.style.angle=std::uint8_t(mix(b.seed)%8);e.style.grain.resize(64);for(std::size_t i=0;i<64;i++)e.style.grain[i]=std::uint8_t(160+(mix(b.seed+i)%96));return e;
}
}
