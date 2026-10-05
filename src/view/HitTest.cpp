#include "Viewport.h"
#include <cmath>
namespace drawborad { namespace {
Result<DocumentPoint> inversePoint(const LayerTransform& t,DocumentPoint p){auto a=t.affine[0],b=t.affine[1],c=t.affine[2],d=t.affine[3],x=t.affine[4],y=t.affine[5];auto det=a*d-b*c;if(!std::isfinite(det)||std::abs(det)<1e-12)return error(ErrorCode::InvalidInput,"hittest.singular");auto px=p.x-x,py=p.y-y;return DocumentPoint{(d*px-c*py)/det,(-b*px+a*py)/det};}
bool inside(Rect b,DocumentPoint p){return p.x>=b.x&&p.y>=b.y&&p.x<double(b.x+std::int64_t(b.width))&&p.y<double(b.y+std::int64_t(b.height));}}
Result<NodeId> hitTest(const DocumentSnapshot& s,DocumentPoint p){auto root=s.nodes->find(Document::rootId());if(root==s.nodes->end())return error(ErrorCode::CorruptData,"hittest.root");for(auto it=root->second.children.rbegin();it!=root->second.children.rend();++it){auto n=s.nodes->find(*it);if(n==s.nodes->end()||!n->second.visible||n->second.locked)continue;auto q=inversePoint(n->second.transform,p);if(!q)continue;if(inside(n->second.bounds,q.value()))return n->first;}return error(ErrorCode::InvalidInput,"hittest.none");}
}
