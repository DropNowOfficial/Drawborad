#include "Test.h"
#include "codecs/CodecProcessProtocol.h"
using namespace drawborad;

TEST("C03 codec process payload round trips bounded decoded pixels"){
    CodecPayload payload;
    payload.info.originalSize={1,1};
    payload.info.orientedSize={1,1};
    payload.info.format=ImageFormat::Bmp;
    payload.pixels.width=1;
    payload.pixels.height=1;
    payload.pixels.format={ColorMode::RGB,Depth::U8,false};
    payload.pixels.rowStride=3;
    payload.pixels.bytes={10,20,30};
    CodecLimits limits;
    limits.decodedBytes=1024;
    auto encoded=encodeCodecPayload(payload,limits);
    CHECK(encoded);
    auto decoded=decodeCodecPayload(encoded.value(),limits);
    CHECK(decoded);
    CHECK(decoded.value().pixels.width==1);
    CHECK(decoded.value().pixels.height==1);
    CHECK(decoded.value().pixels.bytes==payload.pixels.bytes);
}
