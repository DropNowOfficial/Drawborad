#include "codecs/OraStream.h"
#include "storage/FileIO.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <random>
using namespace drawborad;
namespace fs=std::filesystem;
TileData pixels(unsigned w,unsigned h,std::uint8_t r,std::uint8_t g,std::uint8_t b){
 TileData t;t.width=w;t.height=h;t.rowStride=std::uint64_t(w)*4;t.bytes.resize(t.rowStride*h);
 for(std::size_t i=0;i<t.bytes.size();i+=4){t.bytes[i]=r;t.bytes[i+1]=g;t.bytes[i+2]=b;t.bytes[i+3]=255;}return t;
}
Bytes read(const fs::path& p){std::ifstream f(p,std::ios::binary);assert(f);return Bytes(std::istreambuf_iterator<char>(f),{});}
int main(int argc,char** argv){
 const auto dir=fs::temp_directory_path()/("ora-stream-"+std::to_string(std::random_device{}()));assert(fs::create_directory(dir));
 auto merged=pixels(1725,2625,0,255,255);
 // Black pixel in the Multiply lineart; cyan color layer remains independently editable.
 merged.bytes[0]=merged.bytes[1]=merged.bytes[2]=0;
 ExportPlan plan;plan.request.format=ExportFormat::Ora;plan.request.path=dir/"painting.ora";plan.request.size={1725,2625};
 RasterEncodingOptions options;
 if(argc>2)options.iccProfile=read(argv[2]);
 auto tinyPng=encodePngBounded(pixels(8,8,255,0,0),options,4096);assert(tinyPng && tinyPng.value().capacity()<=4096);
 unsigned calls=0;
 OraLayerProvider provider=[&](std::size_t i,std::stop_token)->Result<ArchiveLayer>{
  calls++;ArchiveLayer layer;layer.name=i==0?"Lineart & <original>":i==1?"Hidden test":"Color";
  layer.pixels=pixels(1725,2625,i==0?255:0,255,255);
  if(i==0){layer.blend=BlendMode::Multiply;layer.pixels.bytes[0]=layer.pixels.bytes[1]=layer.pixels.bytes[2]=0;}
  if(i==1){layer.visible=false;layer.opacity=.375;}
  return layer;
 };
 auto done=writeOraStream(plan,3,provider,merged,options);assert(done && calls==3);
 NativeFileIO io;assert(io.hash(plan.request.path,2ull*1024*1024*1024).value()==done.value().hashes.front());
 const auto before=read(plan.request.path);assert(!writeOraStream(plan,3,provider,merged,options));assert(read(plan.request.path)==before);
 plan.request.overwrite=true;
 auto failProvider=[&](std::size_t i,std::stop_token stop)->Result<ArchiveLayer>{if(i==1)return error(ErrorCode::IoError,"test.provider");return provider(i,stop);};
 assert(!writeOraStream(plan,3,failProvider,merged,options));assert(read(plan.request.path)==before);
 std::stop_source cancel;
 auto cancelProvider=[&](std::size_t i,std::stop_token stop)->Result<ArchiveLayer>{auto layer=provider(i,stop);cancel.request_stop();return layer;};
 assert(!writeOraStream(plan,3,cancelProvider,merged,options,{},cancel.get_token()));assert(read(plan.request.path)==before);
 OraStreamLimits small;small.maxPngBytes=32;assert(!writeOraStream(plan,3,provider,merged,options,small));assert(read(plan.request.path)==before);
 small={};small.maxOutputBytes=100;assert(!writeOraStream(plan,3,provider,merged,options,small));assert(read(plan.request.path)==before);
 auto invalid=[&](std::size_t i,std::stop_token st)->Result<ArchiveLayer>{auto layer=provider(i,st);layer.value().pixels.rowStride=1;return layer;};
 assert(!writeOraStream(plan,3,invalid,merged,options));assert(read(plan.request.path)==before);
 const auto savedPath=plan.request.path;plan.request.path=dir/"race.ora";plan.request.overwrite=false;
 auto racing=[&](std::size_t i,std::stop_token st)->Result<ArchiveLayer>{if(i==0){std::ofstream f(plan.request.path,std::ios::binary);f<<"concurrent owner";}return provider(i,st);};
 assert(!writeOraStream(plan,3,racing,merged,options));assert(read(plan.request.path)==Bytes({'c','o','n','c','u','r','r','e','n','t',' ','o','w','n','e','r'}));
 fs::remove(plan.request.path);plan.request.path=savedPath;
 for(const auto& e:fs::directory_iterator(dir))assert(e.path().filename()=="painting.ora");
 if(argc>1)fs::copy_file(plan.request.path,argv[1],fs::copy_options::overwrite_existing);
 fs::remove_all(dir);std::cout<<"ORA stream tests passed\n";
}
