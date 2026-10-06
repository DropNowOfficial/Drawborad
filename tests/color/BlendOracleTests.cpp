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
    Bytes fake(128,0);fake[3]=128;
    fake[16]='R';fake[17]='G';fake[18]='B';fake[19]=' ';
    fake[36]='a';fake[37]='c';fake[38]='s';fake[39]='p';
    auto profile=store.import(fake);CHECK(profile);
    TileData tile;tile.width=1;tile.height=1;tile.format={ColorMode::RGB,Depth::U8,true};
    tile.profileId=profile.value();tile.rowStride=4;tile.bytes={32,64,128,255};
    ColorService service(store);
    ProofRequest request{tile,
        {profile.value(),profile.value(),ColorIntent::Relative,true,1},
        {profile.value(),profile.value(),ColorIntent::Relative,true,1},
        true,true};
    auto result=softProof(request,service);
    CHECK(!result);
}
