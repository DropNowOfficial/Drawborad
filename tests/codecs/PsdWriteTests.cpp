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
