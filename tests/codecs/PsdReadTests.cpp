#include "Test.h"
#include "codecs/psd/PsdReader.h"
#include "codecs/psd/PsdWriter.h"
#include <filesystem>
#include <fstream>
using namespace drawborad;

namespace {
void p16(Bytes& b,std::uint16_t v){b.push_back(std::uint8_t(v>>8));b.push_back(std::uint8_t(v));}
void p32(Bytes& b,std::uint32_t v){b.push_back(std::uint8_t(v>>24));b.push_back(std::uint8_t(v>>16));b.push_back(std::uint8_t(v>>8));b.push_back(std::uint8_t(v));}
void p64(Bytes& b,std::uint64_t v){p32(b,std::uint32_t(v>>32));p32(b,std::uint32_t(v));}

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
    const std::array<std::array<std::uint8_t,10>,4> zipPrediction{{
        {{120,156,227,146,3,0,0,52,0,41}},
        {{120,156,19,145,3,0,0,72,0,51}},
        {{120,156,147,147,3,0,0,92,0,61}},
        {{120,156,251,223,8,0,2,129,1,129}}
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
        }else if(compression==2){
            channelData.insert(channelData.end(),zip[channel].begin(),zip[channel].end());
        }else{
            channelData.insert(channelData.end(),zipPrediction[channel].begin(),zipPrediction[channel].end());
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

TEST("C04 unsupported Photoshop color mode is rejected during header preflight"){
    auto bytes=psd(1,1,1);
    bytes[24]=0;
    bytes[25]=1; // grayscale
    CodecLimits limits;
    auto result=readPhotoshopHeader(bytes,limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedFeature);
    CHECK(result.error().messageKey=="psd.color-mode");
}

TEST("C04 Photoshop color modes require their base channel count"){
    CodecLimits limits;
    auto rgb=psd(1,1,1);
    rgb[12]=0;
    rgb[13]=2;
    auto rgbResult=readPhotoshopHeader(rgb,limits);
    CHECK(!rgbResult);
    CHECK(rgbResult.error().code==ErrorCode::CorruptData);
    CHECK(rgbResult.error().messageKey=="psd.channel-count");

    auto cmyk=psd(1,1,1);
    cmyk[12]=0;
    cmyk[13]=3;
    cmyk[24]=0;
    cmyk[25]=4;
    auto cmykResult=readPhotoshopHeader(cmyk,limits);
    CHECK(!cmykResult);
    CHECK(cmykResult.error().code==ErrorCode::CorruptData);
    CHECK(cmykResult.error().messageKey=="psd.channel-count");
}

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

TEST("C04 ZIP prediction channel planes decode to exact interleaved RGBA pixels"){
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(compressedLayerPsd(3),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().layers.size()==1);
    CHECK(decoded.value().layers[0].pixels.bytes==Bytes({10,20,30,255,40,50,60,128}));
}

namespace {
Bytes zipPrediction16LayerPsd(){
    const std::array<std::array<std::uint8_t,12>,4> zip{{
        {{120,156,99,100,98,98,2,0,0,20,0,8}},
        {{120,156,19,20,98,97,1,0,0,138,0,44}},
        {{120,156,83,84,98,99,3,0,1,0,0,80}},
        {{120,156,251,255,191,161,17,0,8,126,3,0}}
    }};

    Bytes records;
    p16(records,1);
    p32(records,0);p32(records,0);p32(records,1);p32(records,2);
    p16(records,4);
    for(std::uint16_t channel=0;channel<4;++channel){
        p16(records,channel<3?channel:std::uint16_t(0xffff));
        p32(records,14);
    }
    records.insert(records.end(),{'8','B','I','M','n','o','r','m'});
    records.push_back(255);records.push_back(0);records.push_back(0);records.push_back(0);
    Bytes extra;
    p32(extra,0);p32(extra,0);
    extra.push_back(1);extra.push_back('H');extra.push_back(0);extra.push_back(0);
    p32(records,std::uint32_t(extra.size()));
    records.insert(records.end(),extra.begin(),extra.end());

    Bytes channelData;
    for(const auto& plane:zip){
        p16(channelData,3);
        channelData.insert(channelData.end(),plane.begin(),plane.end());
    }

    Bytes layerInfo=records;
    layerInfo.insert(layerInfo.end(),channelData.begin(),channelData.end());
    if(layerInfo.size()%2)layerInfo.push_back(0);
    Bytes layerMask;
    p32(layerMask,std::uint32_t(layerInfo.size()));
    layerMask.insert(layerMask.end(),layerInfo.begin(),layerInfo.end());
    p32(layerMask,0);

    Bytes b{'8','B','P','S'};
    p16(b,1);b.insert(b.end(),6,0);p16(b,4);p32(b,1);p32(b,2);p16(b,16);p16(b,3);
    p32(b,0);p32(b,0);
    p32(b,std::uint32_t(layerMask.size()));
    b.insert(b.end(),layerMask.begin(),layerMask.end());
    p16(b,0);
    return b;
}
}

TEST("C04 16-bit ZIP prediction preserves channel values"){
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(zipPrediction16LayerPsd(),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().layers.size()==1);
    CHECK(decoded.value().layers[0].pixels.bytes==Bytes({
        0x02,0x01, 0x12,0x11, 0x22,0x21, 0xff,0xff,
        0x04,0x03, 0x16,0x15, 0x28,0x27, 0x80,0x80
    }));
}


namespace {
Bytes rgbFlatComposite(std::uint16_t compression){
    Bytes b{'8','B','P','S'};
    p16(b,1);
    b.insert(b.end(),6,0);
    p16(b,4);
    p32(b,1);
    p32(b,2);
    p16(b,8);
    p16(b,3);
    p32(b,0);
    p32(b,0);
    p32(b,0);
    p16(b,compression);
    if(compression==0){
        b.insert(b.end(),{10,147,20,152,30,157,255,128});
    }else if(compression==1){
        for(int row=0;row<4;++row)p16(b,3);
        const std::array<std::array<std::uint8_t,2>,4> rows{{
            {{10,147}},{{20,152}},{{30,157}},{{255,128}}
        }};
        for(const auto& row:rows){
            b.push_back(1);
            b.insert(b.end(),row.begin(),row.end());
        }
    }else if(compression==2){
        b.insert(b.end(),{120,156,227,154,44,50,67,110,238,255,6,0,12,154,3,132});
    }else if(compression==3){
        b.insert(b.end(),{120,156,227,234,20,105,145,171,255,223,8,0,11,151,3,73});
    }
    return b;
}
}

// Photoshop stores RGB merged previews with transparency matted against white.
// These fixture planes are the on-disk values for straight pixels
// {10,20,30,255} and {40,50,60,128}; the decoder must remove that matte.
void checkFlatComposite(std::uint16_t compression){
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(rgbFlatComposite(compression),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().layers.empty());
    CHECK(decoded.value().composite.has_value());
    CHECK(decoded.value().composite->format==PixelFormat{ColorMode::RGB,Depth::U8,true});
    CHECK(decoded.value().composite->bytes==Bytes({10,20,30,255,40,50,60,128}));
}

TEST("C04 flattened PSD without layer section decodes raw merged composite"){
    checkFlatComposite(0);
}

TEST("C04 rejects nonzero reserved PSD header bytes"){
    auto bytes=rgbFlatComposite(0);
    bytes[6]=1;
    CodecLimits limits;
    auto result=readPhotoshopHeader(bytes,limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::CorruptData);
    CHECK(result.error().messageKey=="psd.header-reserved");
}

TEST("C04 flattened PSD decodes RLE merged composite"){
    checkFlatComposite(1);
}

TEST("C04 flattened PSD decodes ZIP merged composite"){
    checkFlatComposite(2);
}

TEST("C04 flattened PSD decodes ZIP prediction merged composite"){
    checkFlatComposite(3);
}

namespace {
Bytes rgbFlatPsbRleComposite(){
    Bytes b{'8','B','P','S'};
    p16(b,2);
    b.insert(b.end(),6,0);
    p16(b,4);
    p32(b,1);
    p32(b,2);
    p16(b,8);
    p16(b,3);
    p32(b,0);
    p32(b,0);
    p64(b,0);
    p16(b,1);
    for(int row=0;row<4;++row)p32(b,3);
    const std::array<std::array<std::uint8_t,2>,4> rows{{
        {{10,147}},{{20,152}},{{30,157}},{{255,128}}
    }};
    for(const auto& row:rows){
        b.push_back(1);
        b.insert(b.end(),row.begin(),row.end());
    }
    return b;
}
}

TEST("C04 flattened PSB RLE uses four-byte scanline counts"){
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(rgbFlatPsbRleComposite(),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().info.format==ImageFormat::Psb);
    CHECK(decoded.value().composite.has_value());
    CHECK(decoded.value().composite->bytes==Bytes({10,20,30,255,40,50,60,128}));
}

namespace {
Bytes layeredRgbCompositeWithLayerCount(std::int16_t count){
    auto b=compressedLayerPsd(1);
    std::size_t at=26;
    const auto read32=[](const Bytes& bytes,std::size_t pos){
        return (std::uint32_t(bytes[pos])<<24)|(std::uint32_t(bytes[pos+1])<<16)|
               (std::uint32_t(bytes[pos+2])<<8)|bytes[pos+3];
    };
    for(int section=0;section<2;++section){
        const auto length=read32(b,at);
        at+=4+length;
    }
    at+=4; // layer/mask section length
    const auto layerInfoLength=read32(b,at);
    at+=4;
    if(layerInfoLength>=2){
        const auto encoded=std::uint16_t(count);
        b[at]=std::uint8_t(encoded>>8);
        b[at+1]=std::uint8_t(encoded);
    }
    b.insert(b.end(),{10,147,20,152,30,157,255,128});
    return b;
}
}

TEST("C04 positive layer count does not reinterpret saved alpha as transparency"){
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(
        layeredRgbCompositeWithLayerCount(1),ProfileId{},limits);
    CHECK(!decoded);
    CHECK(decoded.error().code==ErrorCode::UnsupportedFeature);
    CHECK(decoded.error().messageKey=="psd.composite-extra-channels");
}

TEST("C04 negative layer count marks merged transparency"){
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(
        layeredRgbCompositeWithLayerCount(-1),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().composite.has_value());
    CHECK(decoded.value().composite->format==PixelFormat{ColorMode::RGB,Depth::U8,true});
    CHECK(decoded.value().composite->bytes==Bytes({10,20,30,255,40,50,60,128}));
}


namespace {
Bytes rawCmykComposite(std::uint16_t depth,std::span<const std::uint8_t> planes){
    Bytes b{'8','B','P','S'};
    p16(b,1);
    b.insert(b.end(),6,0);
    p16(b,4);
    p32(b,1);
    p32(b,1);
    p16(b,depth);
    p16(b,4);
    p32(b,0);
    p32(b,0);
    // Non-empty layer/mask section with a valid zero-layer layer-info block.
    // This keeps the test focused on CMYK polarity instead of composite-only parsing.
    p32(b,10);
    p32(b,2);
    p16(b,0);
    p32(b,0);
    p16(b,0);
    b.insert(b.end(),planes.begin(),planes.end());
    return b;
}
}

TEST("C04 CMYK8 PSD file polarity decodes to native ink coverage"){
    const Bytes planes{255,191,127,0};
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(rawCmykComposite(8,planes),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().composite.has_value());
    CHECK(decoded.value().composite->format==PixelFormat{ColorMode::CMYK,Depth::U8,false});
    CHECK(decoded.value().composite->bytes==Bytes({0,64,128,255}));
}

TEST("C04 CMYK16 PSD file polarity decodes to native ink coverage"){
    const Bytes planes{0xff,0xff, 0xbf,0xff, 0x7f,0xff, 0x00,0x00};
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(rawCmykComposite(16,planes),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().composite.has_value());
    CHECK(decoded.value().composite->format==PixelFormat{ColorMode::CMYK,Depth::U16,false});
    CHECK(decoded.value().composite->bytes==
          Bytes({0x00,0x00, 0x00,0x40, 0x00,0x80, 0xff,0xff}));
}

namespace {
Bytes psdWithUnsupportedLayerTag(const char key[5]){
    Bytes records;
    p16(records,1);
    p32(records,0);p32(records,0);p32(records,1);p32(records,1);
    p16(records,0);
    records.insert(records.end(),{'8','B','I','M','n','o','r','m'});
    records.push_back(255);records.push_back(0);records.push_back(0);records.push_back(0);
    Bytes extra;
    p32(extra,0);
    p32(extra,0);
    extra.push_back(1);extra.push_back('L');extra.push_back(0);extra.push_back(0);
    extra.insert(extra.end(),{'8','B','I','M'});
    extra.insert(extra.end(),key,key+4);
    p32(extra,0);
    p32(records,std::uint32_t(extra.size()));
    records.insert(records.end(),extra.begin(),extra.end());
    Bytes layerInfo=records;
    if(layerInfo.size()%2)layerInfo.push_back(0);
    Bytes layerMask;
    p32(layerMask,std::uint32_t(layerInfo.size()));
    layerMask.insert(layerMask.end(),layerInfo.begin(),layerInfo.end());
    p32(layerMask,0);
    Bytes b{'8','B','P','S'};
    p16(b,1);b.insert(b.end(),6,0);p16(b,4);p32(b,1);p32(b,1);p16(b,8);p16(b,3);
    p32(b,0);p32(b,0);p32(b,std::uint32_t(layerMask.size()));
    b.insert(b.end(),layerMask.begin(),layerMask.end());
    return b;
}
}

namespace {
Bytes zeroChannelLayerPsd(std::optional<std::uint32_t> sectionType){
    Bytes records;
    p16(records,1);
    p32(records,0);p32(records,0);p32(records,1);p32(records,1);
    p16(records,0);
    records.insert(records.end(),{'8','B','I','M','n','o','r','m'});
    records.push_back(255);records.push_back(0);records.push_back(0);records.push_back(0);
    Bytes extra;
    p32(extra,0);
    p32(extra,0);
    extra.push_back(1);extra.push_back('L');extra.push_back(0);extra.push_back(0);
    if(sectionType){
        extra.insert(extra.end(),{'8','B','I','M','l','s','c','t'});
        p32(extra,4);
        p32(extra,*sectionType);
    }
    p32(records,std::uint32_t(extra.size()));
    records.insert(records.end(),extra.begin(),extra.end());
    Bytes layerInfo=records;
    if(layerInfo.size()%2)layerInfo.push_back(0);
    Bytes layerMask;
    p32(layerMask,std::uint32_t(layerInfo.size()));
    layerMask.insert(layerMask.end(),layerInfo.begin(),layerInfo.end());
    p32(layerMask,0);
    Bytes b{'8','B','P','S'};
    p16(b,1);b.insert(b.end(),6,0);p16(b,3);p32(b,1);p32(b,1);p16(b,8);p16(b,3);
    p32(b,0);p32(b,0);p32(b,std::uint32_t(layerMask.size()));
    b.insert(b.end(),layerMask.begin(),layerMask.end());
    return b;
}
}

TEST("C04 ordinary pixel layer requires complete base color channels"){
    CodecLimits limits;
    auto result=readPhotoshopHeader(zeroChannelLayerPsd(std::nullopt),limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::CorruptData);
    CHECK(result.error().messageKey=="psd.missing-color-channels");
}

TEST("C04 section divider may omit pixel color channels"){
    CodecLimits limits;
    auto result=readPhotoshopHeader(zeroChannelLayerPsd(1),limits);
    CHECK(result);
    CHECK(result.value().layers.size()==1);
    CHECK(result.value().layers[0].sectionType==1u);
}

TEST("C04 smart object layer data is rejected instead of silently claimed editable"){
    CodecLimits limits;
    auto result=readPhotoshopHeader(psdWithUnsupportedLayerTag("SoLE"),limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedFeature);
    CHECK(result.error().messageKey=="psd.unsupported-layer-feature");
}

TEST("C04 text layer data is rejected instead of silently claimed editable"){
    CodecLimits limits;
    auto result=readPhotoshopHeader(psdWithUnsupportedLayerTag("TySh"),limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedFeature);
    CHECK(result.error().messageKey=="psd.unsupported-layer-feature");
}

TEST("C04 legacy type tool layer data is rejected instead of silently claimed editable"){
    CodecLimits limits;
    auto result=readPhotoshopHeader(psdWithUnsupportedLayerTag("tySh"),limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedFeature);
    CHECK(result.error().messageKey=="psd.unsupported-layer-feature");
}

TEST("C04 legacy placed layer data is rejected instead of silently claimed editable"){
    CodecLimits limits;
    auto result=readPhotoshopHeader(psdWithUnsupportedLayerTag("plLd"),limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedFeature);
    CHECK(result.error().messageKey=="psd.unsupported-layer-feature");
}

TEST("C04 external linked layer data is rejected instead of silently claimed editable"){
    CodecLimits limits;
    auto result=readPhotoshopHeader(psdWithUnsupportedLayerTag("lnkE"),limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedFeature);
    CHECK(result.error().messageKey=="psd.unsupported-layer-feature");
}


namespace {
Bytes psdWithDocumentLinkedInfo(){
    auto b=compressedLayerPsd(1);
    const auto read32=[](const Bytes& bytes,std::size_t at){
        return (std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|
               (std::uint32_t(bytes[at+2])<<8)|bytes[at+3];
    };
    std::size_t at=26;
    for(int section=0;section<2;++section){
        const auto length=read32(b,at);
        at+=4+length;
    }
    const auto layerMaskLengthOffset=at;
    const auto layerMaskLength=read32(b,at);
    const auto layerMaskBegin=at+4;
    const auto layerMaskEnd=layerMaskBegin+layerMaskLength;
    CHECK(layerMaskEnd<=b.size());

    Bytes linked{'8','B','I','M','l','n','k','E'};
    p32(linked,0);

    Bytes out;
    out.insert(out.end(),b.begin(),b.begin()+std::ptrdiff_t(layerMaskLengthOffset));
    p32(out,layerMaskLength+std::uint32_t(linked.size()));
    out.insert(out.end(),b.begin()+std::ptrdiff_t(layerMaskBegin),
               b.begin()+std::ptrdiff_t(layerMaskEnd));
    out.insert(out.end(),linked.begin(),linked.end());
    out.insert(out.end(),b.begin()+std::ptrdiff_t(layerMaskEnd),b.end());
    return out;
}
}

TEST("C04 document linked-file info is rejected even when main layer info exists"){
    CodecLimits limits;
    auto result=readPhotoshopHeader(psdWithDocumentLinkedInfo(),limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedFeature);
    CHECK(result.error().messageKey=="psd.unsupported-document-feature");
}

namespace {
Bytes psdWithDuplicateLayerChannelId(){
    auto b=compressedLayerPsd(1);
    const auto read32=[](const Bytes& bytes,std::size_t at){
        return (std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|
               (std::uint32_t(bytes[at+2])<<8)|bytes[at+3];
    };
    std::size_t at=26;
    for(int section=0;section<2;++section){
        const auto length=read32(b,at);
        at+=4+length;
    }
    at+=4; // layer/mask length
    at+=4; // layer-info length
    at+=2; // layer count
    at+=16; // first layer rectangle
    at+=2; // channel count
    const auto firstChannel=at;
    const auto secondChannel=firstChannel+6;
    CHECK(secondChannel+1<b.size());
    b[secondChannel]=b[firstChannel];
    b[secondChannel+1]=b[firstChannel+1];
    return b;
}
}

TEST("C04 duplicate layer channel IDs are corrupt"){
    CodecLimits limits;
    auto result=decodePhotoshopLayers(
        psdWithDuplicateLayerChannelId(),ProfileId{},limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::CorruptData);
    CHECK(result.error().messageKey=="psd.channel-duplicate");
}

namespace {
Bytes psdWithFirstLayerChannelId(std::int16_t id){
    auto b=compressedLayerPsd(1);
    const auto read32=[](const Bytes& bytes,std::size_t at){
        return (std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|
               (std::uint32_t(bytes[at+2])<<8)|bytes[at+3];
    };
    std::size_t at=26;
    for(int section=0;section<2;++section){
        const auto length=read32(b,at);
        at+=4+length;
    }
    at+=4; // layer/mask length
    at+=4; // layer-info length
    at+=2; // layer count
    at+=16; // first layer rectangle
    at+=2; // channel count
    const auto encoded=std::uint16_t(id);
    b[at]=std::uint8_t(encoded>>8);
    b[at+1]=std::uint8_t(encoded);
    return b;
}
}

TEST("C04 real user mask channel is explicit unsupported instead of silently dropped"){
    CodecLimits limits;
    auto result=decodePhotoshopLayers(
        psdWithFirstLayerChannelId(-3),ProfileId{},limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedFeature);
    CHECK(result.error().messageKey=="psd.unsupported-layer-channel");
}

TEST("C04 extra layer color channel is explicit unsupported instead of silently dropped"){
    CodecLimits limits;
    auto result=decodePhotoshopLayers(
        psdWithFirstLayerChannelId(7),ProfileId{},limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedFeature);
    CHECK(result.error().messageKey=="psd.unsupported-layer-channel");
}

namespace {
std::size_t firstLayerFixedFields(Bytes& b){
    const auto read32=[](const Bytes& bytes,std::size_t at){
        return (std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|
               (std::uint32_t(bytes[at+2])<<8)|bytes[at+3];
    };
    std::size_t at=26;
    for(int section=0;section<2;++section){
        const auto length=read32(b,at);
        at+=4+length;
    }
    at+=4; // layer/mask length
    at+=4; // layer-info length
    at+=2; // layer count
    at+=16; // layer rectangle
    const auto channels=std::uint16_t(
        (std::uint16_t(b[at])<<8)|b[at+1]);
    at+=2+std::size_t(channels)*6u;
    return at; // blend signature starts here
}
Bytes psdWithFirstLayerClipping(std::uint8_t clipping){
    auto b=compressedLayerPsd(1);
    const auto at=firstLayerFixedFields(b);
    CHECK(at+12<=b.size());
    b[at+9]=clipping;
    return b;
}
Bytes maskedPsdWithFlags(std::uint8_t flags){
    PsdWriteRequest q;
    q.size={1,1};
    q.format={ColorMode::RGB,Depth::U8,true};
    PsdLayerPixels layer;
    layer.name="Mask flags";
    layer.pixels.width=1;
    layer.pixels.height=1;
    layer.pixels.format=q.format;
    layer.pixels.rowStride=4;
    layer.pixels.bytes={10,20,30,255};
    layer.mask={255};
    layer.maskBounds={0,0,1,1};
    q.layers.push_back(layer);
    auto encoded=encodePsd(q);
    CHECK(encoded);
    auto b=encoded.value();
    const auto fixed=firstLayerFixedFields(b);
    const auto extraLengthAt=fixed+12;
    CHECK(extraLengthAt+8<=b.size());
    const auto maskBody=extraLengthAt+4+4;
    CHECK(maskBody+18<=b.size());
    b[maskBody+17]=flags;
    return b;
}
Bytes maskedPsdWith16BitSample(std::uint16_t sample){
    const auto read32=[](const Bytes& bytes,std::size_t at){
        return (std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|
               (std::uint32_t(bytes[at+2])<<8)|bytes[at+3];
    };
    PsdWriteRequest q;
    q.size={1,1};
    q.format={ColorMode::RGB,Depth::U16,true};
    PsdLayerPixels layer;
    layer.name="Mask16";
    layer.pixels.width=1;
    layer.pixels.height=1;
    layer.pixels.format=q.format;
    layer.pixels.rowStride=8;
    layer.pixels.bytes={0,0,0,0,0,0,0xff,0xff};
    layer.mask={0};
    layer.maskBounds={0,0,1,1};
    q.layers.push_back(layer);
    auto encoded=encodePsd(q);
    CHECK(encoded);
    auto b=encoded.value();
    const auto fixed=firstLayerFixedFields(b);
    const auto extraLengthAt=fixed+12;
    CHECK(extraLengthAt+4<=b.size());
    const auto extraLength=read32(b,extraLengthAt);
    const auto channelData=extraLengthAt+4+extraLength;
    const auto maskSample=channelData+4u*4u+2u;
    CHECK(maskSample+2<=b.size());
    b[maskSample]=std::uint8_t(sample>>8);
    b[maskSample+1]=std::uint8_t(sample);
    return b;
}
}

TEST("C04 clipping layer semantics are explicit unsupported"){
    CodecLimits limits;
    auto result=readPhotoshopHeader(psdWithFirstLayerClipping(1),limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedFeature);
    CHECK(result.error().messageKey=="psd.layer-clipping");
}

TEST("C04 nonzero user-mask flags are explicit unsupported"){
    CodecLimits limits;
    auto result=readPhotoshopHeader(maskedPsdWithFlags(1),limits);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedFeature);
    CHECK(result.error().messageKey=="psd.mask-flags");
}

TEST("C04 16-bit user mask quantizes full range to internal 8-bit"){
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(
        maskedPsdWith16BitSample(0x0182),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().layers.size()==1);
    CHECK(decoded.value().layers[0].mask==Bytes({2}));
}

TEST("C04 file-level Photoshop import uses bounded decoder and marks imported source"){
    auto path=std::filesystem::temp_directory_path()/"drawborad-c04-file-import.psd";
    auto bytes=compressedLayerPsd(1);
    {
        std::ofstream out(path,std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    }
    ImportPolicy policy;
    policy.limits.encodedBytes=1024*1024;
    auto imported=readPhotoshop(path,policy);
    CHECK(imported);
    CHECK(imported.value().sourceKind==SourceKind::ImportedRaster);
    CHECK(imported.value().photoshop.layers.size()==1);
    CHECK(imported.value().photoshop.layers[0].pixels.bytes==Bytes({10,20,30,255,40,50,60,128}));
    std::filesystem::remove(path);
}

namespace {
Bytes psbWithLongAdditionalInfoThenUnicode(){
    Bytes records;
    p16(records,1);
    p32(records,0);p32(records,0);p32(records,1);p32(records,1);
    p16(records,0);
    records.insert(records.end(),{'8','B','I','M','n','o','r','m'});
    records.push_back(255);records.push_back(0);records.push_back(0);records.push_back(0);

    Bytes extra;
    p32(extra,0);
    p32(extra,0);
    extra.push_back(1);extra.push_back('L');extra.push_back(0);extra.push_back(0);

    extra.insert(extra.end(),{'8','B','I','M','L','r','1','6'});
    p64(extra,0);

    Bytes unicode;
    p32(unicode,1);
    p16(unicode,0x5c42);
    extra.insert(extra.end(),{'8','B','I','M','l','u','n','i'});
    p32(extra,std::uint32_t(unicode.size()));
    extra.insert(extra.end(),unicode.begin(),unicode.end());

    p32(records,std::uint32_t(extra.size()));
    records.insert(records.end(),extra.begin(),extra.end());

    Bytes layerInfo=records;
    if(layerInfo.size()%2)layerInfo.push_back(0);

    Bytes layerMask;
    p64(layerMask,std::uint64_t(layerInfo.size()));
    layerMask.insert(layerMask.end(),layerInfo.begin(),layerInfo.end());
    p32(layerMask,0);

    Bytes b{'8','B','P','S'};
    p16(b,2);b.insert(b.end(),6,0);p16(b,4);p32(b,1);p32(b,1);p16(b,8);p16(b,3);
    p32(b,0);p32(b,0);
    p64(b,std::uint64_t(layerMask.size()));
    b.insert(b.end(),layerMask.begin(),layerMask.end());
    return b;
}
}

TEST("C04 PSB long additional-layer lengths do not hide following Unicode metadata"){
    CodecLimits limits;
    auto result=readPhotoshopHeader(psbWithLongAdditionalInfoThenUnicode(),limits);
    CHECK(result);
    CHECK(result.value().layers.size()==1);
    CHECK(result.value().layers[0].name=="层");
}
