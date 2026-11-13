#pragma once
#include "brush/BucketFill.h"
#include "core/HiddenBoundary.h"
#include "view/LayerTree.h"
namespace drawborad {
inline Result<Command> hiddenBoundaryFillCommand(const DocumentSnapshot&s,NodeId source,NodeId layer,Id event,std::uint32_t x,std::uint32_t y,std::uint8_t threshold,ColorValue color,std::stop_token stop={})try{
 auto valid=validateSnapshot(s);if(!valid)return valid.error();const auto w=s.spec.pixelSize.width,h=s.spec.pixelSize.height;
 if(w*h>1048576)return error(ErrorCode::BudgetExceeded,"boundary.fill-canvas-limit");
 if(x>=w||y>=h||!s.nodes->contains(source)||!s.nodes->contains(layer))return error(ErrorCode::InvalidInput,"boundary.fill-target");
 bool foundFill=false,foundInk=false;
 for(const auto id:s.nodes->at(Document::rootId()).children){const auto&n=s.nodes->at(id);if(n.stage==hiddenBoundary::Stage)continue;
  if(id==layer&&!foundFill&&!foundInk&&nativeLayerPaintable(s,id)&&nativeLayerSupported(s,id)&&n.maskId.empty()){foundFill=true;continue;}
  if(id==source&&foundFill&&!foundInk&&n.stage==LineartStage&&n.locked&&n.visible&&n.opacity==1&&n.maskId.empty()&&n.transform==LayerTransform{}){foundInk=true;continue;}
  return error(ErrorCode::UnsupportedFeature,"boundary.fill-stack");
 }
 if(!foundFill||!foundInk)return error(ErrorCode::UnsupportedFeature,"boundary.fill-stack");
 auto raw=lineartRasterPixels(s,source);if(!raw)return raw.error();
 TileData ref;ref.width=std::uint32_t(w);ref.height=std::uint32_t(h);ref.rowStride=w*4;ref.format={ColorMode::RGB,Depth::U8,true};ref.bytes.resize(w*h*4);
 for(std::size_t i=0;i<w*h;i++){if(i%65536==0&&stop.stop_requested())return error(ErrorCode::Cancelled,"boundary.cancelled");const auto*p=raw.value().data()+i*4;
  // Declared original lineart only: RGB luminance over white, never recolor the source.
  const double a=p[3]/255.;const double gray=(.2126*p[0]+.7152*p[1]+.0722*p[2])*a+255*(1-a);const std::uint8_t v=gray<threshold?0:255;ref.bytes[i*4]=ref.bytes[i*4+1]=ref.bytes[i*4+2]=v;ref.bytes[i*4+3]=255;
 }
 std::uint64_t work=0;
 for(const auto&[id,node]:*s.nodes)if(node.stage==hiddenBoundary::Stage){auto parsed=hiddenBoundary::parse(s,node);if(!parsed)return parsed.error();const auto&v=parsed.value();if(!v.enabled||v.lineart!=source)continue;
  for(std::size_t i=1;i<v.points.size();i++){const auto a=v.points[i-1],b=v.points[i];const auto minx=std::max(0,int(std::floor(std::min(a.x,b.x)-v.radius))),miny=std::max(0,int(std::floor(std::min(a.y,b.y)-v.radius)));const auto maxx=std::min(int(w)-1,int(std::ceil(std::max(a.x,b.x)+v.radius))),maxy=std::min(int(h)-1,int(std::ceil(std::max(a.y,b.y)+v.radius)));
   work+=std::uint64_t(maxx-minx+1)*(maxy-miny+1);if(work>16777216)return error(ErrorCode::BudgetExceeded,"boundary.raster-work");const auto dx=b.x-a.x,dy=b.y-a.y,den=dx*dx+dy*dy;
   for(int yy=miny;yy<=maxy;yy++){if(stop.stop_requested())return error(ErrorCode::Cancelled,"boundary.cancelled");for(int xx=minx;xx<=maxx;xx++){const double px=xx+.5,py=yy+.5;const double t=den?std::clamp(((px-a.x)*dx+(py-a.y)*dy)/den,0.,1.):0.;if(std::hypot(px-a.x-t*dx,py-a.y-t*dy)<=v.radius){auto k=(std::size_t(yy)*w+xx)*4;ref.bytes[k]=ref.bytes[k+1]=ref.bytes[k+2]=0;}}}
  }
 }
 if(ref.bytes[(y*w+x)*4]==0)return error(ErrorCode::InvalidInput,"boundary.seed-blocked");
 return bucketFillCommand(s,layer,event,ref,x,y,0,color,stop);
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"boundary.allocation");}
}
