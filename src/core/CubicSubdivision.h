#pragma once
#include "Document.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stop_token>
namespace drawborad {
struct CubicInkGeometry {std::vector<InputSample> samples;std::size_t visits{};};
struct CubicExpansionLimits {std::size_t points=4096,visits=8191;};
namespace cubic_ink_detail {
struct Point {double x{},y{};};
inline Point blend(Point a,Point b,double t){return {a.x*(1-t)+b.x*t,a.y*(1-t)+b.y*t};}
inline double distance(Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y);}
struct Node {std::array<Point,4> c;unsigned depth;};
}
// One mathematical primitive for23 and24. The output owns a shared budget across
// every segment in one event; limits are checked BEFORE another visit/emission.
inline Result<void> appendCubicSegment(const std::array<InputSample,4>& source,CubicInkGeometry& out,CubicExpansionLimits limit,std::stop_token stop={}){
 using namespace cubic_ink_detail;
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 std::array<Point,4> controls;double scale=1;
 for(std::size_t i=0;i<4;++i){controls[i]={source[i].x,source[i].y};scale=std::max({scale,std::abs(controls[i].x),std::abs(controls[i].y)});}
 const double margin=scale*std::numeric_limits<double>::epsilon()*4096;
 std::array<Node,18> stack;std::size_t pending=1;stack[0]={controls,0};
 if(out.samples.empty()){if(!limit.points)return error(ErrorCode::BudgetExceeded,"cubic.flatten-points");out.samples.push_back(source.front());}
 while(pending){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
  if(out.visits>=limit.visits)return error(ErrorCode::BudgetExceeded,"cubic.flatten-work");
  ++out.visits;const auto n=stack[--pending];const auto& p=n.c;
  const double bound=std::max(distance(p[1],blend(p[0],p[3],1./3)),distance(p[2],blend(p[0],p[3],2./3)))+margin;
  if(bound<=1./16){
   if(out.samples.size()>=limit.points)return error(ErrorCode::BudgetExceeded,"cubic.flatten-points");
   auto sample=source.front();sample.x=p[3].x;sample.y=p[3].y;out.samples.push_back(sample);continue;
  }
  if(n.depth>=16||pending+2>stack.size())return error(ErrorCode::BudgetExceeded,"cubic.flatten-depth");
  const auto a=blend(p[0],p[1],.5),b=blend(p[1],p[2],.5),d=blend(p[2],p[3],.5),f=blend(a,b,.5),g=blend(b,d,.5),h=blend(f,g,.5);
  stack[pending++]={{h,g,d,p[3]},n.depth+1};stack[pending++]={{p[0],a,f,h},n.depth+1};
 }
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 return {};
}
}
