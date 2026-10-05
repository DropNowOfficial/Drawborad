#include "Test.h"
#include "codecs/psd/PsdWriter.h"
using namespace drawborad;
TEST("C05 layered PSD writer emits layer records readable by independent header parser"){PsdWriteRequest q;q.size={2,2};q.format={ColorMode::RGB,Depth::U8,true};PsdLayerPixels l;l.name="Layer";l.pixels.width=2;l.pixels.height=2;l.pixels.format=q.format;l.pixels.rowStride=8;l.pixels.bytes.assign(16,255);q.layers.push_back(l);auto b=encodePsd(q);CHECK(b);CodecLimits lim;auto r=readPhotoshopHeader(b.value(),lim);CHECK(r);CHECK(r.value().layers.size()==1);CHECK(r.value().size==q.size);}
TEST("C05 refuses PSD dimensions above format limit"){PsdWriteRequest q;q.size={30001,1};q.format={ColorMode::RGB,Depth::U8,true};CHECK(!encodePsd(q));}
