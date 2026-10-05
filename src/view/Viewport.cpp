#include "Viewport.h"
#include <cmath>
namespace drawborad {
Result<void> Viewport::configure(double z,double d,double x,double y){if(!std::isfinite(z)||!std::isfinite(d)||!std::isfinite(x)||!std::isfinite(y)||z<=0||z>256||d<=0||d>16)return error(ErrorCode::InvalidInput,"viewport.params");scale_=z;dpr_=d;tx_=x;ty_=y;return{};}
DocumentPoint Viewport::toDocument(ScreenPoint p)const{return{(p.x-tx_)/scale_,(p.y-ty_)/scale_};}
ScreenPoint Viewport::toScreen(DocumentPoint p)const{return{p.x*scale_+tx_,p.y*scale_+ty_};}
Result<void> Viewport::zoomAt(double f,ScreenPoint a){if(!std::isfinite(f)||f<=0)return error(ErrorCode::InvalidInput,"viewport.zoom");auto p=toDocument(a);auto nz=scale_*f;if(nz<.01||nz>256)return error(ErrorCode::InvalidInput,"viewport.zoom");scale_=nz;tx_=a.x-p.x*scale_;ty_=a.y-p.y*scale_;return{};}
}
