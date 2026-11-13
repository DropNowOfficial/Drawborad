#include "brush/ClipPolygonSelection.h"
#include "brush/HollowInkRegion.h"
#include <cassert>
#include <limits>
#include <iostream>
using namespace drawborad;
static std::vector<unsigned char> pixels(const FillMask& m,unsigned w,unsigned h){std::vector<unsigned char> out(w*h);for(auto r:m.runs)for(unsigned x=r.x;x<r.x+r.length;x++)out[r.y*w+x]=1;return out;}
int main(){
 const unsigned w=32,h=24;FillMask source;for(unsigned y=0;y<h;y++){source.runs.push_back({y,0,w});source.pixelCount+=w;}const auto saved=source;
 const std::vector<DocumentPoint> tilted{{7,2},{27,7},{22,21},{2,16}};
 auto cut=clipPolygonSelection(source,w,h,tilted);assert(cut);auto raster=pixels(cut.value(),w,h);
 // Independent convex half-plane oracle, not an axis-aligned bounding box.
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  const double px=x+.5,py=y+.5;bool inside=true;
  for(std::size_t i=0;i<tilted.size();i++){auto a=tilted[i],b=tilted[(i+1)%tilted.size()];const auto cross=(b.x-a.x)*(py-a.y)-(b.y-a.y)*(px-a.x);inside&=cross>0;}
  // This polygon's edges do not pass through the checked centers.
  assert(bool(raster[y*w+x])==inside);
 }
 assert(!raster[3*w+3]&&raster[10*w+15]);assert(source.runs==saved.runs&&source.pixelCount==saved.pixelCount);
 auto reversed=tilted;std::reverse(reversed.begin(),reversed.end());auto reverseCut=clipPolygonSelection(source,w,h,reversed);assert(reverseCut&&reverseCut.value().runs==cut.value().runs);
 std::vector<unsigned char> ink(w*h,1);auto hollow=hollowInkRegion(ink,cut.value(),w,h,1);assert(hollow);auto cleared=pixels(hollow.value().toClear,w,h);
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){bool expected=x>0&&y>0&&x+1<w&&y+1<h;if(expected)for(unsigned yy=y-1;yy<=y+1;yy++)for(unsigned xx=x-1;xx<=x+1;xx++)expected&=bool(raster[yy*w+xx]);assert(bool(cleared[y*w+x])==expected);}
 const std::vector<DocumentPoint> concave{{2,2},{20,2},{20,20},{14,20},{14,8},{8,8},{8,20},{2,20}};
 auto concaveCut=clipPolygonSelection(source,w,h,concave);assert(concaveCut);auto cr=pixels(concaveCut.value(),w,h);assert(cr[5*w+10]&&!cr[12*w+10]&&cr[12*w+4]&&cr[12*w+17]);
 // Intersect with sparse source, preserving holes and one-pixel islands.
 FillMask sparse;sparse.runs={{5,5,1},{10,0,32},{12,4,2}};sparse.pixelCount=35;auto sparseCut=clipPolygonSelection(sparse,w,h,concave);assert(sparseCut);auto sr=pixels(sparseCut.value(),w,h);auto originalSparse=pixels(sparse,w,h);for(unsigned i=0;i<w*h;i++)assert(sr[i]==(cr[i]&&originalSparse[i]));
 // Centers exactly on a diagonal follow the declared half-open rule.
 const std::vector<DocumentPoint> diagonal{{0,0},{8,8},{0,8}};auto diagonalCut=clipPolygonSelection(source,w,h,diagonal);assert(diagonalCut);auto dr=pixels(diagonalCut.value(),w,h);for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)assert(bool(dr[y*w+x])==(y<8&&x<y));
 const std::vector<DocumentPoint> full{{0,0},{32,0},{32,24},{0,24}};auto fullCut=clipPolygonSelection(source,w,h,full);assert(fullCut&&fullCut.value().runs==source.runs);
 const std::vector<std::vector<DocumentPoint>> invalid{
  {{1,1},{2,2}},{{1,1},{8,8},{1,8},{8,1}},{{0,0},{5,0},{5,5},{0,0}},
  {{1,1},{1,1},{5,5}},{{1,1},{2,2},{3,3}},{{1,1},{5,1},{3,1},{3,8}},
  {{-1,0},{3,0},{3,3}},{{0,0},{33,0},{3,3}},
  {{0,0},{std::numeric_limits<double>::quiet_NaN(),0},{3,3}},
  {{0,0},{std::numeric_limits<double>::infinity(),0},{3,3}},
  {{1,1},{8,1},{8,8},{4,1},{1,8}} // nonadjacent edge touch
 };
 for(const auto& p:invalid){assert(!validateClipPolygon(p,w,h));assert(!clipPolygonSelection(source,w,h,p));}
 auto many=full;many.resize(513);assert(!validateClipPolygon(many,w,h));assert(!validateClipPolygon(full,8192,8192));
 auto bad=source;bad.pixelCount++;assert(!clipPolygonSelection(bad,w,h,full));bad=source;bad.runs.back().length=33;assert(!clipPolygonSelection(bad,w,h,tilted));bad=source;bad.runs[1]=bad.runs[0];assert(!clipPolygonSelection(bad,w,h,full));
 FillMask isolated;isolated.runs={{0,0,1}};isolated.pixelCount=1;auto empty=clipPolygonSelection(isolated,w,h,tilted);assert(!empty&&empty.error().messageKey=="clip-polygon.empty-intersection");
 std::stop_source stop;stop.request_stop();auto cancelled=clipPolygonSelection(source,w,h,full,stop.get_token());assert(!cancelled&&cancelled.error().code==ErrorCode::Cancelled);assert(!validateClipPolygon(full,w,h,stop.get_token()));
 std::cout<<"PASS polygon clip: tilted/concave exact pixel selection, source intersection, hollow boundary preservation, invalid/simple/finite/cancel guards\n";
}
