#include "Test.h"
#include "codecs/psd/PsdReader.h"
using namespace drawborad;

namespace {
void p16(Bytes& b,std::uint16_t v){b.push_back(std::uint8_t(v>>8));b.push_back(std::uint8_t(v));}
void p32(Bytes& b,std::uint32_t v){b.push_back(std::uint8_t(v>>24));b.push_back(std::uint8_t(v>>16));b.push_back(std::uint8_t(v>>8));b.push_back(std::uint8_t(v));}

Bytes psd(std::uint16_t version,std::uint32_t w,std::uint32_t h){
    Bytes b{'8','B','P','S'};p16(b,version);b.insert(b.end(),6,0);p16(b,4);p32(b,h);p32(b,w);p16(b,8);p16(b,3);p32(b,0);p32(b,0);
    if(version==1){p32(b,0);}else{p32(b,0);p32(b,0);}
    return b;
}

Bytes compressedLayerPsd(std::uint16_t compression){
    const std::array<std::array<std::uint8_t,2>,4> planes{{{{10,40}},{{20,50}},{{30,60}},{{255,128}}}};
    const std::array<std::array<std::uint8_t,10>,4> zip{{
        {{120,156,227,210,0,0,0,62,0,51}},
        {{120,156,19,49,2,0,0,92,0,71}},
        {{120,156,147,179,1,0,0,122,0,91}},
        {{120,156,251,223,0,0,2,128,1,128}}
    }};

    Bytes records;
    p16(records,1);
    p32(records,0);p32(records,0);p32(records,1);p32(records,2);
    p16(records,4);
    const std::uint32_t channelLength = compression==1 ? 7u : 12u;
    for(std::uint16_t channel=0;channel<4;++channel){
        p16(records,channel<3?channel:std::uint16_t(0xffff));
        p32(records,channelLength);
    }
    records.insert(records.end(),{'8','B','I','M','n','o','r','m'});
    records.push_back(255);records.push_back(0);records.push_back(0);records.push_back(0);
    Bytes extra;
    p32(extra,0);p32(extra,0);
    extra.push_back(1);extra.push_back('L');extra.push_back(0);extra.push_back(0);
    p32(records,std::uint32_t(extra.size()));
    records.insert(records.end(),extra.begin(),extra.end());

    Bytes channelData;
    for(std::size_t channel=0;channel<4;++channel){
        p16(channelData,compression);
        if(compression==1){
            p16(channelData,3);
            channelData.push_back(1);
            channelData.push_back(planes[channel][0]);
            channelData.push_back(planes[channel][1]);
        }else{
            channelData.insert(channelData.end(),zip[channel].begin(),zip[channel].end());
        }
    }

    Bytes layerInfo=records;
    layerInfo.insert(layerInfo.end(),channelData.begin(),channelData.end());
    if(layerInfo.size()%2)layerInfo.push_back(0);

    Bytes layerMask;
    p32(layerMask,std::uint32_t(layerInfo.size()));
    layerMask.insert(layerMask.end(),layerInfo.begin(),layerInfo.end());
    p32(layerMask,0);

    Bytes b{'8','B','P','S'};
    p16(b,1);b.insert(b.end(),6,0);p16(b,4);p32(b,1);p32(b,2);p16(b,8);p16(b,3);
    p32(b,0);p32(b,0);
    p32(b,std::uint32_t(layerMask.size()));
    b.insert(b.end(),layerMask.begin(),layerMask.end());
    p16(b,0);
    return b;
}
}

TEST("C04 accepts bounded PSD and PSB headers"){CodecLimits l;auto a=readPhotoshopHeader(psd(1,32,16),l);auto b=readPhotoshopHeader(psd(2,64,32),l);CHECK(a&&b);CHECK(a.value().format==ImageFormat::Psd);CHECK(b.value().format==ImageFormat::Psb);}
TEST("C04 rejects length sections outside file"){auto b=psd(1,32,16);b.resize(30);b[26]=0xff;b[27]=0xff;b[28]=0xff;b[29]=0xff;CodecLimits l;CHECK(!readPhotoshopHeader(b,l));}
TEST("C04 PSD and PSB enforce format dimension limits"){CodecLimits l;CHECK(!readPhotoshopHeader(psd(1,30001,1),l));CHECK(!readPhotoshopHeader(psd(2,300001,1),l));}

TEST("C04 RLE channel planes decode to exact interleaved RGBA pixels"){
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(compressedLayerPsd(1),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().layers.size()==1);
    CHECK(decoded.value().layers[0].pixels.bytes==Bytes({10,20,30,255,40,50,60,128}));
}

TEST("C04 ZIP channel planes decode to exact interleaved RGBA pixels"){
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(compressedLayerPsd(2),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().layers.size()==1);
    CHECK(decoded.value().layers[0].pixels.bytes==Bytes({10,20,30,255,40,50,60,128}));
}
