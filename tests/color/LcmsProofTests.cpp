#include "Test.h"
#include "color/BlendPolicy.h"
#include <lcms2.h>
using namespace drawborad;
namespace {
Bytes srgbProfile(){
    cmsHPROFILE profile=cmsCreate_sRGBProfile();
    if(!profile)return {};
    cmsUInt32Number size=0;
    if(!cmsSaveProfileToMem(profile,nullptr,&size)||!size){cmsCloseProfile(profile);return {};}
    Bytes bytes(size);
    if(!cmsSaveProfileToMem(profile,bytes.data(),&size)){cmsCloseProfile(profile);return {};}
    bytes.resize(size);
    cmsCloseProfile(profile);
    return bytes;
}
}
TEST("C02 real LittleCMS proofing preserves layout and alpha"){
    ProfileStore store;
    auto bytes=srgbProfile();CHECK(!bytes.empty());
    auto profile=store.import(bytes);CHECK(profile);
    TileData tile;tile.width=2;tile.height=1;tile.format={ColorMode::RGB,Depth::U8,true};
    tile.profileId=profile.value();tile.rowStride=8;tile.bytes={64,96,128,77,180,150,100,201};
    ColorService service(store);
    ProofRequest request{tile,
        {profile.value(),profile.value(),ColorIntent::Relative,true,1},
        {profile.value(),profile.value(),ColorIntent::Relative,true,1},
        true,true};
    auto result=softProof(request,service);
    CHECK(result);
    CHECK(result.value().format==tile.format);
    CHECK(result.value().profileId==profile.value());
    CHECK(result.value().bytes.size()==tile.bytes.size());
    CHECK(result.value().bytes[3]==77);
    CHECK(result.value().bytes[7]==201);
}
