#include "Test.h"
#include "codecs/psd/PsdWriter.h"
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
    CHECK(info.value().layers[0].sectionType==2);
}
