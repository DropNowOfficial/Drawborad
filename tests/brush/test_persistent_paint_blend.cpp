#include "core/PersistentRasterPaint.h"
#include "ui/MergedDocument.h"
#include "ui/LayerStream.h"
#include <array>
#include <cassert>
#include <iostream>
using namespace drawborad;
namespace {
using Pixel=std::array<std::uint8_t,4>;
const auto layer=Id::fromNumber(25);
Bytes uniform(Rect r,Pixel p){Bytes b(std::size_t(r.width*r.height*4));for(std::size_t i=0;i<b.size();i+=4)std::copy(p.begin(),p.end(),b.begin()+i);return b;}
Pixel pixel(const Bytes&b,std::size_t width,std::size_t x,std::size_t y){Pixel p;std::copy_n(b.begin()+std::ptrdiff_t((y*width+x)*4),4,p.begin());return p;}
DocumentSnapshot fixture(std::uint8_t strokeOpacity=255,double layerOpacity=1,bool reverse=false){
 NewDocumentRequest request;request.dimensions={768,768,Unit::Px,300};auto spec=validateDocument(request,{});assert(spec);auto doc=Document::create(spec.value(),Id::fromNumber(99));assert(doc);auto s=doc.value().snapshot();assert(s.spec.background.transparent);
 auto resources=std::make_shared<ResourceMap>(*s.resources);auto nodes=std::make_shared<NodeMap>(*s.nodes);
 // Both rectangles cross the 256 index seam and the 512 render tile seam.
 const Rect a{240,230,300,310},b{250,245,300,310};
 auto red=persistentRaster::makePatch(s,a,uniform(a,{255,0,0,128}));auto blue=persistentRaster::makePatch(s,b,uniform(b,{0,0,255,128}));assert(red&&blue);
 (*resources)[red.value().id]=red.value();(*resources)[blue.value().id]=blue.value();
 persistentRaster::History h;h.records={{Id::fromNumber(101),red.value().id,strokeOpacity},{Id::fromNumber(102),blue.value().id,strokeOpacity}};h.cursor=2;if(reverse)std::reverse(h.records.begin(),h.records.end());
 auto m=persistentRaster::manifest(h);assert(m);(*resources)[m.value().id]=m.value();
 LayerNode n;n.id=layer;n.parent=Document::rootId();n.stage=persistentRaster::Stage;n.sourceKind=SourceKind::ImportedRaster;n.sourceResource=m.value().id;n.bounds={0,0,768,768};n.opacity=layerOpacity;n.name="Portable patch blend";
 (*nodes)[layer]=n;nodes->at(Document::rootId()).children.push_back(layer);s.nodes=nodes;s.resources=resources;++s.revision;assert(validateSnapshot(s));return s;
}
Bytes region(const DocumentSnapshot&s,Rect r){auto p=persistentRaster::Prepared::prepare(s);assert(p);Bytes b(std::size_t(r.width*r.height*4),77);assert(p.value().render(s,layer,r,b));return b;}
Bytes partition(const DocumentSnapshot&s,unsigned side){auto p=persistentRaster::Prepared::prepare(s);assert(p);Bytes all(768*768*4);for(unsigned y=0;y<768;y+=side)for(unsigned x=0;x<768;x+=side){Rect r{x,y,std::min(768-x,side),std::min(768-y,side)};Bytes b(std::size_t(r.width*r.height*4));assert(p.value().render(s,layer,r,b));for(unsigned row=0;row<r.height;++row)std::copy_n(b.begin()+std::ptrdiff_t(row*r.width*4),std::size_t(r.width*4),all.begin()+std::ptrdiff_t(((y+row)*768+x)*4));}return all;}
void expect(const Bytes&b,unsigned w,unsigned x,unsigned y,Pixel p){assert(pixel(b,w,x,y)==p);}
}
int main(){
 const auto s=fixture();const auto snapshot=s;
 const auto all=partition(s,512);assert(all==partition(s,256));assert(all==partition(s,127));
 // Independent integer expectations, not calls to the production blend helper:
 // sa=da=128, den=128*255+128*127=48896; round(den/255)=192.
 // red=round(255*128*127/48896)=85; blue=round(255*128*255/48896)=170.
 for(unsigned v:{255u,256u,511u,512u})expect(all,768,v,v,{85,0,170,192});
 expect(all,768,240,230,{255,0,0,128});expect(all,768,549,554,{0,0,255,128});expect(all,768,239,230,{0,0,0,0});expect(all,768,550,555,{0,0,0,0});
 // A non-tile-aligned rectangle must have the exact same document-space pixels.
 const Rect roi{243,237,309,321};const auto local=region(s,roi);for(unsigned y=0;y<roi.height;++y)for(unsigned x=0;x<roi.width;++x)assert(pixel(local,roi.width,x,y)==pixel(all,768,x+roi.x,y+roi.y));
 const auto reversed=region(fixture(255,1,true),{300,300,1,1});expect(reversed,1,0,0,{170,0,85,192});
 // Per-record opacity128 gives effective alpha64 each, den=28544, alpha112.
 // This is NOT the result of compositing first and applying layer opacity0.5.
 const auto perStroke=fixture(128);const auto perLayer=fixture(255,.5);
 expect(region(perStroke,{300,300,1,1}),1,0,0,{109,0,146,112});
 expect(region(perLayer,{300,300,1,1}),1,0,0,{85,0,170,192});
 auto full=renderMergedDocument(s,{s.revision});auto stroke=renderMergedDocument(perStroke,{perStroke.revision});auto layerHalf=renderMergedDocument(perLayer,{perLayer.revision});assert(full&&stroke&&layerHalf);
 assert(full.value().merged.bytes==all);expect(stroke.value().merged.bytes,768,300,300,{109,0,146,112});expect(layerHalf.value().merged.bytes,768,300,300,{85,0,170,96});
 // Portable layer export retains unattenuated pixels plus explicit layer opacity.
 auto stream=NativeLayerStream::create(perLayer,{perLayer.revision});assert(stream&&stream.value().size()==1);auto exported=stream.value().read(0);assert(exported&&exported.value().opacity==.5&&exported.value().blend==BlendMode::Normal);assert(exported.value().pixels.bytes==all);
 // No worker, Qt surface, renderer installation, or resource mutation was needed.
 assert(s.sameContent(snapshot));assert(*s.resources==*snapshot.resources);
 std::cout<<"PASS portable persistent paint: exact ordered straight RGBA, per-stroke versus whole-layer opacity, 256/512/nonaligned tile equivalence, native layer export\n";
}
