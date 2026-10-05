#pragma once
#include "core/Document.h"
namespace drawborad {
struct ScreenPoint{double x{},y{};}; struct DocumentPoint{double x{},y{};};
class Viewport { double scale_=1,dpr_=1,tx_=0,ty_=0; public: Result<void> configure(double zoom,double devicePixelRatio,double panX,double panY); DocumentPoint toDocument(ScreenPoint) const; ScreenPoint toScreen(DocumentPoint) const; Result<void> zoomAt(double factor,ScreenPoint anchor); double zoom()const{return scale_;} double dpr()const{return dpr_;} };
Result<NodeId> hitTest(const DocumentSnapshot&,DocumentPoint);
}
