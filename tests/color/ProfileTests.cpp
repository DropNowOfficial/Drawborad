#include "Test.h"
#include "color/ColorService.h"
#include "core/Commands.h"
using namespace drawborad;
namespace {
Bytes icc(const char* space){
 Bytes b(128,0); b[0]=0;b[1]=0;b[2]=0;b[3]=128;
 b[16]=space[0];b[17]=space[1];b[18]=space[2];b[19]=space[3];
 b[36]='a';b[37]='c';b[38]='s';b[39]='p'; return b;
}
}
TEST("C01 rejects corrupt ICC and channel mismatch"){
 ProfileStore s; Bytes bad(128,0); CHECK(!s.import(bad));
 auto rgb=s.import(icc("RGB ")); auto cmyk=s.import(icc("CMYK")); CHECK(rgb&&cmyk);
 ColorService c(s); ColorSpec spec;spec.format={ColorMode::CMYK,Depth::U16,true};spec.profileId=cmyk.value();
 CHECK(!c.assign(spec,rgb.value())); CHECK(c.assign(spec,cmyk.value()));
}
TEST("C01 same-profile conversion keeps native samples and alpha"){
 ProfileStore s;auto id=s.import(icc("CMYK"));CHECK(id);ColorService c(s);
 TileData t;t.width=1;t.height=1;t.format={ColorMode::CMYK,Depth::U16,true};t.profileId=id.value();t.rowStride=10;t.bytes={1,2,3,4,5,6,7,8,9,10};
 auto out=c.convert(t,{id.value(),id.value(),ColorIntent::Relative,true,1});CHECK(out);CHECK(out.value().bytes==t.bytes);CHECK(out.value().format.mode==ColorMode::CMYK);CHECK(out.value().format.depth==Depth::U16);
}

TEST("C01 ICC conversion accepts documents without alpha"){
 ProfileStore s;
 auto id=s.import(icc("RGB "));
 CHECK(id);
 ColorService service(s);
 TileData tile;
 tile.width=2;
 tile.height=1;
 tile.format={ColorMode::RGB,Depth::U8,false};
 tile.profileId=id.value();
 tile.rowStride=6;
 tile.bytes={10,20,30,40,50,60};
 auto out=service.convert(tile,{id.value(),id.value(),ColorIntent::Relative,true,1});
 CHECK(out);
 CHECK(out.value().format.alpha==false);
 CHECK(out.value().rowStride==6);
 CHECK(out.value().bytes==tile.bytes);
}

TEST("C01 assigning a document profile changes interpretation without pixel resource bytes") {
    ProfileStore profiles;
    auto source=profiles.import(icc("RGB "));
    auto targetBytes=icc("RGB ");
    targetBytes[64]=1;
    auto target=profiles.import(targetBytes);
    CHECK(source);
    CHECK(target);

    NewDocumentRequest request;
    request.dimensions={16,16,Unit::Px,300};
    request.colorSpec.format={ColorMode::RGB,Depth::U8,true};
    request.colorSpec.profileId=source.value();
    auto spec=validateDocument(request,ResourceBudget{});
    CHECK(spec);
    auto document=Document::create(spec.value(),Id::fromNumber(7001));
    CHECK(document);

    Bytes pixels{1,2,3,4,5,6,7,8};
    auto pixelHash=sha256(pixels);
    Resource resource{pixelHash,ResourceKind::Tile,std::make_shared<const Bytes>(pixels),{}};
    CHECK(document.value().apply(Command{InstallResource{resource}},document.value().snapshot().revision));
    auto before=document.value().snapshot();

    ColorService service(profiles);
    auto changed=service.assign(document.value(),target.value());
    CHECK(changed);
    auto after=document.value().snapshot();
    CHECK(after.spec.colorSpec.profileId==target.value());
    CHECK(*after.resources==*before.resources);
    CHECK(*after.nodes==*before.nodes);
    CHECK(after.eventIndex->size()==before.eventIndex->size());
    CHECK(after.revision==before.revision+1);
}
