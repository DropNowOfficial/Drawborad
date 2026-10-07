#include "Test.h"
#include "codecs/psd/PsdWriter.h"
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace drawborad;
TEST("C05 layered PSD writer emits layer records readable by independent header parser"){PsdWriteRequest q;q.size={2,2};q.format={ColorMode::RGB,Depth::U8,true};PsdLayerPixels l;l.name="Layer";l.pixels.width=2;l.pixels.height=2;l.pixels.format=q.format;l.pixels.rowStride=8;l.pixels.bytes.assign(16,255);q.layers.push_back(l);auto b=encodePsd(q);CHECK(b);CodecLimits lim;auto r=readPhotoshopHeader(b.value(),lim);CHECK(r);CHECK(r.value().layers.size()==1);CHECK(r.value().size==q.size);}
TEST("C05 refuses PSD dimensions above format limit"){PsdWriteRequest q;q.size={30001,1};q.format={ColorMode::RGB,Depth::U8,true};CHECK(!encodePsd(q));}

TEST("C05 multiply blend survives layered PSD roundtrip"){
    PsdWriteRequest q; q.size={1,1}; q.format={ColorMode::RGB,Depth::U8,true};
    PsdLayerPixels l; l.name="Multiply"; l.blend=BlendMode::Multiply;
    l.pixels.width=1; l.pixels.height=1; l.pixels.format=q.format; l.pixels.rowStride=4; l.pixels.bytes={10,20,30,255};
    q.layers.push_back(l);
    auto b=encodePsd(q); CHECK(b);
    CodecLimits lim; auto r=readPhotoshopHeader(b.value(),lim); CHECK(r);
    CHECK(r.value().layers.size()==1);
    CHECK(r.value().layers[0].blend==BlendMode::Multiply);
}

TEST("C05 Unicode layer name roundtrips through luni metadata"){
    const std::string unicodeName="\xE5\x89\x8D\xE6\x99\xAF\xE5\xB1\x82";
    PsdWriteRequest q; q.size={1,1}; q.format={ColorMode::RGB,Depth::U8,true};
    PsdLayerPixels l; l.name=unicodeName;
    l.pixels.width=1; l.pixels.height=1; l.pixels.format=q.format;
    l.pixels.rowStride=4; l.pixels.bytes={10,20,30,255};
    q.layers.push_back(l);
    auto encoded=encodePsd(q); CHECK(encoded);
    CodecLimits limits;
    auto parsed=readPhotoshopHeader(encoded.value(),limits); CHECK(parsed);
    CHECK(parsed.value().layers.size()==1);
    CHECK(parsed.value().layers[0].name==unicodeName);
}

TEST("C04 C05 raw layered PSD pixels roundtrip exactly"){
    PsdWriteRequest q; q.size={2,1}; q.format={ColorMode::RGB,Depth::U8,true};
    PsdLayerPixels l; l.name="Pixels";
    l.pixels.width=2; l.pixels.height=1; l.pixels.format=q.format;
    l.pixels.rowStride=8; l.pixels.bytes={10,20,30,255,40,50,60,128};
    q.layers.push_back(l);
    auto encoded=encodePsd(q); CHECK(encoded);
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(encoded.value(),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().layers.size()==1);
    CHECK(decoded.value().layers[0].pixels.bytes==l.pixels.bytes);
    CHECK(decoded.value().layers[0].pixels.format==q.format);
}


TEST("C04 C05 user layer mask roundtrips as channel minus two"){
    PsdWriteRequest q;
    q.size={2,1};
    q.format={ColorMode::RGB,Depth::U8,true};
    PsdLayerPixels l;
    l.name="Masked";
    l.pixels.width=2;
    l.pixels.height=1;
    l.pixels.format=q.format;
    l.pixels.rowStride=8;
    l.pixels.bytes={10,20,30,255,40,50,60,255};
    l.mask={0,255};
    l.maskBounds={0,0,2,1};
    l.maskDefault=255;
    q.layers.push_back(l);
    auto encoded=encodePsd(q);
    CHECK(encoded);
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(encoded.value(),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().layers.size()==1);
    CHECK(decoded.value().layers[0].mask==l.mask);
    CHECK(decoded.value().layers[0].info.maskBounds==l.maskBounds);
    CHECK(decoded.value().layers[0].info.maskDefault==255);
}


TEST("C04 C05 U16 layered PSD samples roundtrip byte-exact"){
    PsdWriteRequest q;
    q.size={1,1};
    q.format={ColorMode::RGB,Depth::U16,true};
    PsdLayerPixels l;
    l.name="U16";
    l.pixels.width=1;
    l.pixels.height=1;
    l.pixels.format=q.format;
    l.pixels.rowStride=8;
    l.pixels.bytes={0x34,0x12, 0x78,0x56, 0xbc,0x9a, 0xfe,0xff};
    q.layers.push_back(l);
    auto encoded=encodePsd(q);
    CHECK(encoded);
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(encoded.value(),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().layers.size()==1);
    CHECK(decoded.value().layers[0].pixels.bytes==l.pixels.bytes);
}


TEST("C04 C05 Photoshop section divider metadata roundtrips"){
    PsdWriteRequest q;
    q.size={1,1};
    q.format={ColorMode::RGB,Depth::U8,true};
    PsdLayerPixels l;
    l.name="Group";
    l.pixels.width=1;
    l.pixels.height=1;
    l.pixels.format=q.format;
    l.pixels.rowStride=4;
    l.pixels.bytes={0,0,0,0};
    l.sectionType=2;
    q.layers.push_back(l);
    auto encoded=encodePsd(q);
    CHECK(encoded);
    CodecLimits limits;
    auto info=readPhotoshopHeader(encoded.value(),limits);
    CHECK(info);
    CHECK(info.value().layers.size()==1);
    CHECK(info.value().layers[0].sectionType==2u);
}


TEST("C05 PSD preserves ICC resource and document resolution"){
    PsdWriteRequest q;
    q.size={1,1};
    q.format={ColorMode::RGB,Depth::U8,true};
    q.iccProfile={0x00,0x01,0x02,0x03,0x7f,0x80,0xfe,0xff};
    q.ppi=300.0;
    PsdLayerPixels l;
    l.name="Layer";
    l.pixels.width=1;
    l.pixels.height=1;
    l.pixels.format=q.format;
    l.pixels.rowStride=4;
    l.pixels.bytes={1,2,3,255};
    q.layers.push_back(l);
    auto encoded=encodePsd(q);
    CHECK(encoded);
    CodecLimits limits;
    auto info=readPhotoshopHeader(encoded.value(),limits);
    CHECK(info);
    CHECK(info.value().iccProfile==q.iccProfile);
    CHECK(std::abs(info.value().ppi-q.ppi)<0.01);
}

TEST("C05 merged composite carries real pixels and alpha") {
    PsdWriteRequest q;
    q.size={1,1};
    q.format={ColorMode::RGB,Depth::U8,true};
    PsdLayerPixels layer;
    layer.name="Merged source";
    layer.pixels.width=1;
    layer.pixels.height=1;
    layer.pixels.format=q.format;
    layer.pixels.rowStride=4;
    layer.pixels.bytes={12,34,56,78};
    q.layers.push_back(layer);
    q.merged=layer.pixels;
    auto encoded=encodePsd(q);
    CHECK(encoded);
    CodecLimits limits;
    auto decoded=decodePhotoshopLayers(encoded.value(),ProfileId{},limits);
    CHECK(decoded);
    CHECK(decoded.value().composite.has_value());
    CHECK(decoded.value().composite->format==q.format);
    CHECK(decoded.value().composite->bytes==layer.pixels.bytes);
}

TEST("C05 alpha merged image uses negative PSD layer count"){
    PsdWriteRequest q;
    q.size={1,1};
    q.format={ColorMode::RGB,Depth::U8,true};
    PsdLayerPixels layer;
    layer.name="Alpha";
    layer.pixels.width=1;
    layer.pixels.height=1;
    layer.pixels.format=q.format;
    layer.pixels.rowStride=4;
    layer.pixels.bytes={12,34,56,78};
    q.layers.push_back(layer);
    q.merged=layer.pixels;
    auto encoded=encodePsd(q);
    CHECK(encoded);

    const auto read32=[](const Bytes& bytes,std::size_t pos){
        return (std::uint32_t(bytes[pos])<<24)|(std::uint32_t(bytes[pos+1])<<16)|
               (std::uint32_t(bytes[pos+2])<<8)|bytes[pos+3];
    };
    std::size_t at=26;
    for(int section=0;section<2;++section){
        const auto length=read32(encoded.value(),at);
        at+=4+length;
    }
    const auto layerMaskLength=read32(encoded.value(),at);
    at+=4;
    CHECK(layerMaskLength>=6);
    const auto layerInfoLength=read32(encoded.value(),at);
    at+=4;
    CHECK(layerInfoLength>=2);
    const auto rawCount=std::uint16_t(
        (std::uint16_t(encoded.value()[at])<<8)|encoded.value()[at+1]);
    CHECK(std::int16_t(rawCount)==-1);
}


namespace {
std::uint32_t readBe32(const Bytes& bytes,std::size_t at){
    return (std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|
           (std::uint32_t(bytes[at+2])<<8)|bytes[at+3];
}
std::size_t compositePayloadOffset(const Bytes& bytes){
    if(bytes.size()<30) return SIZE_MAX;
    std::size_t pos=26;
    for(int section=0;section<3;++section){
        if(pos+4>bytes.size()) return SIZE_MAX;
        const auto n=readBe32(bytes,pos);
        pos+=4;
        if(n>bytes.size()-pos) return SIZE_MAX;
        pos+=n;
    }
    if(pos+2>bytes.size()||bytes[pos]!=0||bytes[pos+1]!=0) return SIZE_MAX;
    return pos+2;
}
PsdWriteRequest cmykRequest(Depth depth,Bytes pixel){
    PsdWriteRequest q;
    q.size={1,1};
    q.format={ColorMode::CMYK,depth,false};
    PsdLayerPixels layer;
    layer.name="CMYK";
    layer.pixels.width=1;
    layer.pixels.height=1;
    layer.pixels.format=q.format;
    layer.pixels.rowStride=pixel.size();
    layer.pixels.bytes=std::move(pixel);
    q.layers.push_back(layer);
    q.merged=q.layers.front().pixels;
    return q;
}
}

TEST("C05 CMYK8 PSD file planes use Photoshop inverted ink polarity"){
    auto q=cmykRequest(Depth::U8,{0,64,128,255});
    auto encoded=encodePsd(q);
    CHECK(encoded);
    const auto at=compositePayloadOffset(encoded.value());
    CHECK(at!=SIZE_MAX);
    CHECK(at+4<=encoded.value().size());
    CHECK(Bytes(encoded.value().begin()+std::ptrdiff_t(at),
                encoded.value().begin()+std::ptrdiff_t(at+4))==
          Bytes({255,191,127,0}));
}

TEST("C05 CMYK16 PSD file planes use Photoshop inverted ink polarity"){
    auto q=cmykRequest(Depth::U16,{0x00,0x00, 0x00,0x40, 0x00,0x80, 0xff,0xff});
    auto encoded=encodePsd(q);
    CHECK(encoded);
    const auto at=compositePayloadOffset(encoded.value());
    CHECK(at!=SIZE_MAX);
    CHECK(at+8<=encoded.value().size());
    CHECK(Bytes(encoded.value().begin()+std::ptrdiff_t(at),
                encoded.value().begin()+std::ptrdiff_t(at+8))==
          Bytes({0xff,0xff, 0xbf,0xff, 0x7f,0xff, 0x00,0x00}));
}

TEST("C05 rejects PSD estimated above two GiB before pixel traversal"){
    PsdWriteRequest q;
    q.size={30000,30000};
    q.format={ColorMode::RGB,Depth::U16,true};
    PsdLayerPixels layer;
    layer.name="Huge";
    layer.pixels.width=30000;
    layer.pixels.height=30000;
    layer.pixels.format=q.format;
    // Deliberately omit backing bytes. The output-size gate must run before
    // stride/pixel traversal and reject a PSD that cannot fit the format.
    q.layers.push_back(layer);
    q.merged=layer.pixels;
    auto encoded=encodePsd(q);
    CHECK(!encoded);
    CHECK(encoded.error().code==ErrorCode::BudgetExceeded);
    CHECK(encoded.error().messageKey=="psd.output-size-limit");
}

TEST("C05 writePsd never replaces an existing file without overwrite consent"){
    auto path=std::filesystem::temp_directory_path()/"drawborad-c05-no-clobber.psd";
    const Bytes old{9,8,7,6};
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(old.data()),std::streamsize(old.size()));}
    PsdWriteRequest q;
    q.path=path;q.size={1,1};q.format={ColorMode::RGB,Depth::U8,true};q.overwrite=false;
    PsdLayerPixels l;l.name="Layer";l.pixels.width=1;l.pixels.height=1;l.pixels.format=q.format;l.pixels.rowStride=4;l.pixels.bytes={10,20,30,255};
    q.layers.push_back(l);q.merged=l.pixels;
    auto result=writePsd(q);
    CHECK(!result);
    std::ifstream in(path,std::ios::binary);Bytes actual((std::istreambuf_iterator<char>(in)),{});in.close();
    CHECK(actual==old);
    std::filesystem::remove(path);
}


TEST("C05 file writer keeps internal staging buffer bounded") {
    PsdWriteRequest request;
    request.path=std::filesystem::temp_directory_path()/"drawborad-c05-stream.psd";
    request.size={1024,1024};
    request.format={ColorMode::RGB,Depth::U8,true};
    PsdLayerPixels layer;
    layer.name="stream";
    layer.pixels.width=1024;
    layer.pixels.height=1024;
    layer.pixels.format=request.format;
    layer.pixels.rowStride=4096;
    layer.pixels.bytes.assign(std::size_t(layer.pixels.rowStride)*layer.pixels.height,127);
    request.layers.push_back(layer);
    request.merged=layer.pixels;
    request.overwrite=true;
    PsdWriteStats stats;
    auto written=writePsd(request,{},&stats);
    CHECK(written);
    CHECK(stats.stagedBytes>8*1024*1024);
    CHECK(stats.peakWriterBufferBytes<=1024*1024);
    std::filesystem::remove(request.path);
}


TEST("C04 Lr16 additional layer info is used when main 16-bit layer info is empty"){
    auto read32=[](const Bytes& bytes,std::size_t at){
        return (std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|
               (std::uint32_t(bytes[at+2])<<8)|bytes[at+3];
    };
    auto put32=[](Bytes& bytes,std::uint32_t v){
        bytes.push_back(std::uint8_t(v>>24));bytes.push_back(std::uint8_t(v>>16));
        bytes.push_back(std::uint8_t(v>>8));bytes.push_back(std::uint8_t(v));
    };
    PsdWriteRequest q;
    q.size={1,1};
    q.format={ColorMode::RGB,Depth::U16,true};
    PsdLayerPixels layer;
    layer.name="Lr16";
    layer.pixels.width=1;layer.pixels.height=1;layer.pixels.format=q.format;
    layer.pixels.rowStride=8;
    layer.pixels.bytes={0x34,0x12,0x78,0x56,0xbc,0x9a,0xfe,0xff};
    q.layers.push_back(layer);
    auto encoded=encodePsd(q);CHECK(encoded);

    const auto& source=encoded.value();
    std::size_t at=26;
    auto colorLength=read32(source,at);at+=4+colorLength;
    auto resourceLength=read32(source,at);at+=4+resourceLength;
    const auto layerMaskLengthOffset=at;
    const auto layerMaskLength=read32(source,at);at+=4;
    const auto layerMaskBegin=at;
    const auto layerMaskEnd=layerMaskBegin+layerMaskLength;
    CHECK(layerMaskEnd<=source.size());
    const auto mainLength=read32(source,at);at+=4;
    CHECK(mainLength>=2);
    CHECK(at+mainLength<=layerMaskEnd);

    Bytes replacement;
    put32(replacement,0);
    put32(replacement,0);
    replacement.insert(replacement.end(),{'8','B','I','M','L','r','1','6'});
    put32(replacement,mainLength);
    replacement.insert(replacement.end(),source.begin()+std::ptrdiff_t(at),
                       source.begin()+std::ptrdiff_t(at+mainLength));
    if(mainLength&1u)replacement.push_back(0);

    Bytes lr16;
    lr16.insert(lr16.end(),source.begin(),source.begin()+std::ptrdiff_t(layerMaskLengthOffset));
    put32(lr16,std::uint32_t(replacement.size()));
    lr16.insert(lr16.end(),replacement.begin(),replacement.end());
    lr16.insert(lr16.end(),source.begin()+std::ptrdiff_t(layerMaskEnd),source.end());

    CodecLimits limits;
    auto header=readPhotoshopHeader(lr16,limits);CHECK(header);
    CHECK(header.value().layers.size()==1);
    CHECK(header.value().layers[0].name=="Lr16");
    auto decoded=decodePhotoshopLayers(lr16,ProfileId{},limits);CHECK(decoded);
    CHECK(decoded.value().layers.size()==1);
    CHECK(decoded.value().layers[0].pixels.bytes==layer.pixels.bytes);
}


TEST("C05 hidden layer visibility survives PSD roundtrip"){
    PsdWriteRequest q;
    q.size={1,1};
    q.format={ColorMode::RGB,Depth::U8,true};
    PsdLayerPixels layer;
    layer.name="hidden";
    layer.visible=false;
    layer.pixels.width=1;layer.pixels.height=1;layer.pixels.format=q.format;
    layer.pixels.rowStride=4;
    layer.pixels.bytes={1,2,3,255};
    q.layers.push_back(layer);
    auto encoded=encodePsd(q);CHECK(encoded);
    CodecLimits limits;
    auto info=readPhotoshopHeader(encoded.value(),limits);CHECK(info);
    CHECK(info.value().layers.size()==1);
    CHECK(!info.value().layers[0].visible);
}
