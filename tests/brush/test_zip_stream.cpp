#include "codecs/ZipStream.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace drawborad;
int main(int argc,char** argv){
 Bytes output;ZipStream zip([&](auto b)->Result<void>{output.insert(output.end(),b.begin(),b.end());return {};});
 std::string mime="image/openraster";
 assert(zip.addKnown("mimetype",std::span(reinterpret_cast<const std::uint8_t*>(mime.data()),mime.size())));
 assert(zip.begin("data/test.txt"));Bytes a{'h','e'},b{'l','l','o'};assert(zip.write(a));assert(zip.write(b));assert(zip.end());
 Bytes blob(200000);for(std::size_t i=0;i<blob.size();i++)blob[i]=std::uint8_t(i%251);
 assert(zip.begin("data/彩色.bin"));assert(zip.write(blob));assert(zip.end());
 assert(zip.begin("empty"));assert(zip.end());assert(zip.finish());assert(zip.bytesWritten()==output.size());
 if(argc>1){std::ofstream f(argv[1],std::ios::binary);f.write(reinterpret_cast<const char*>(output.data()),std::streamsize(output.size()));assert(f.good());}
 assert(!zip.begin("late"));
 auto sink=[](auto)->Result<void>{return {};};
 ZipStream traversal(sink);assert(!traversal.begin("../bad"));assert(!traversal.begin("good"));
 ZipStream order(sink);assert(!order.write(a));assert(!order.finish());
 ZipStream duplicate(sink);assert(duplicate.addKnown("x",{}));assert(!duplicate.begin("x"));
 ZipStreamLimits tiny;tiny.maxBytes=29;ZipStream limit(sink,tiny);assert(!limit.begin("x"));
 tiny={};tiny.maxEntries=1;ZipStream count(sink,tiny);assert(count.addKnown("x",{}));assert(!count.begin("y"));
 tiny={};tiny.maxMetadataBytes=0;ZipStream meta(sink,tiny);assert(!meta.begin("x"));
 tiny={};tiny.maxNameBytes=1;ZipStream name(sink,tiny);assert(!name.begin("xx"));
 std::stop_source stop;ZipStream cancel(sink,{},stop.get_token());assert(cancel.begin("x"));stop.request_stop();assert(!cancel.write(a));assert(!cancel.end());
 unsigned calls=0;ZipStream failing([&](auto)->Result<void>{if(++calls==2)return error(ErrorCode::IoError,"test.write");return {};});
 assert(failing.begin("x"));assert(!failing.write(a));assert(!failing.finish());assert(calls==2);
 for(auto path:{"a//b","/abs","x/../y","C:x","x\\y"}){ZipStream bad(sink);assert(!bad.begin(path));}
 ZipStream nul(sink);assert(!nul.begin(std::string("bad\0name",8)));
 ZipStream invalidUtf8(sink);assert(!invalidUtf8.begin(std::string("\xc0\xaf",2)));
 ZipStream unfinished(sink);assert(unfinished.begin("x"));assert(!unfinished.finish());
 ZipStreamLimits enough;enough.maxBytes=100; // known empty x: 31 local + 47 central + 22 EOCD
 ZipStream exact(sink,enough);assert(exact.addKnown("x",{}));assert(exact.finish());assert(exact.bytesWritten()==100);
 enough.maxBytes=99;ZipStream shortOne(sink,enough);assert(shortOne.addKnown("x",{}));assert(!shortOne.finish());
 ZipStream central(sink,ZipStreamLimits{32});assert(central.addKnown("x",{}));assert(!central.finish());
 std::stop_source during;unsigned chunks=0;
 ZipStream midCancel([&](auto b0)->Result<void>{assert(b0.size()<=65536);if(++chunks==2)during.request_stop();return {};},{},during.get_token());
 assert(midCancel.begin("x"));assert(!midCancel.write(blob));assert(!midCancel.end());assert(chunks==2);
 ZipStream throwing([](auto)->Result<void>{throw std::runtime_error("sink");});assert(!throwing.begin("x"));assert(!throwing.finish());
 std::cout<<"zip stream tests passed\n";
}
