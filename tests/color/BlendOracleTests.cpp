#include "Test.h"
#include "color/BlendPolicy.h"
using namespace drawborad;
namespace {
TileData tile(ColorMode m,Depth d,std::initializer_list<std::uint8_t> v){TileData t;t.width=1;t.height=1;t.format={m,d,true};t.rowStride=std::uint64_t(v.size());t.bytes=v;return t;}
}
TEST("C02 CMYKA8 normal blend preserves five-channel layout"){
 auto a=tile(ColorMode::CMYK,Depth::U8,{255,0,0,0,255});auto b=tile(ColorMode::CMYK,Depth::U8,{0,0,0,255,255});
 auto r=blendPixels({a,b,BlendMode::Normal,1});CHECK(r);CHECK(r.value().bytes.size()==5);CHECK(r.value().bytes[0]==255);CHECK(r.value().bytes[4]==255);
}
TEST("C02 unsupported layout returns error instead of RGB fallback"){
 auto a=tile(ColorMode::RGB,Depth::U8,{255,0,0,255});auto b=tile(ColorMode::CMYK,Depth::U8,{0,0,0,0,255});
 CHECK(!blendPixels({a,b,BlendMode::Multiply,1}));
}

TEST("C02 CMYK multiply uses inverted ink-domain math, not RGB channel multiply") {
    auto src=tile(ColorMode::CMYK,Depth::U8,{128,0,0,0,255});
    auto dst=tile(ColorMode::CMYK,Depth::U8,{128,0,0,0,255});
    auto result=blendPixels({src,dst,BlendMode::Multiply,1});
    CHECK(result);
    CHECK(result.value().bytes[0] > 180);
    CHECK(result.value().bytes[0] < 196);
    CHECK(result.value().bytes[4] == 255);
}

TEST("C02 proof flags are never silently ignored"){
    ProfileStore store;
    auto fake=[](const char* space){
        Bytes b(128,0);b[3]=128;
        b[16]=space[0];b[17]=space[1];b[18]=space[2];b[19]=space[3];
        b[36]='a';b[37]='c';b[38]='s';b[39]='p';
        return b;
    };
    auto document=store.import(fake("RGB "));
    auto proofBytes=fake("CMYK");proofBytes[64]=1;
    auto proof=store.import(proofBytes);
    auto displayBytes=fake("RGB ");displayBytes[64]=2;
    auto display=store.import(displayBytes);
    CHECK(document&&proof&&display);
    TileData tile;tile.width=1;tile.height=1;tile.format={ColorMode::RGB,Depth::U8,true};
    tile.profileId=document.value();tile.rowStride=4;tile.bytes={32,64,128,255};
    ColorService service(store);
    ProofRequest request{tile,
        {document.value(),proof.value(),ColorIntent::Relative,true,1},
        {proof.value(),display.value(),ColorIntent::Relative,true,1},
        true,true};
    auto result=softProof(request,service);
    CHECK(!result);
}
