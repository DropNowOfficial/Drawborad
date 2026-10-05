#include "Test.h"
#include "codecs/psd/PsdReader.h"
using namespace drawborad;
namespace {void p16(Bytes&b,std::uint16_t v){b.push_back(v>>8);b.push_back(v);}void p32(Bytes&b,std::uint32_t v){b.push_back(v>>24);b.push_back(v>>16);b.push_back(v>>8);b.push_back(v);}Bytes psd(std::uint16_t version,std::uint32_t w,std::uint32_t h){Bytes b{'8','B','P','S'};p16(b,version);b.insert(b.end(),6,0);p16(b,4);p32(b,h);p32(b,w);p16(b,8);p16(b,3);p32(b,0);p32(b,0);if(version==1){p32(b,0);}else{p32(b,0);p32(b,0);}return b;}}
TEST("C04 accepts bounded PSD and PSB headers"){CodecLimits l;auto a=readPhotoshopHeader(psd(1,32,16),l);auto b=readPhotoshopHeader(psd(2,64,32),l);CHECK(a&&b);CHECK(a.value().format==ImageFormat::Psd);CHECK(b.value().format==ImageFormat::Psb);}
TEST("C04 rejects length sections outside file"){auto b=psd(1,32,16);b.resize(30);b[26]=0xff;b[27]=0xff;b[28]=0xff;b[29]=0xff;CodecLimits l;CHECK(!readPhotoshopHeader(b,l));}
TEST("C04 PSD and PSB enforce format dimension limits"){CodecLimits l;CHECK(!readPhotoshopHeader(psd(1,30001,1),l));CHECK(!readPhotoshopHeader(psd(2,300001,1),l));}
