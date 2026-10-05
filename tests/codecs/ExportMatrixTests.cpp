#include "Test.h"
#include "codecs/ExportService.h"
#include "codecs/RasterWriters.h"
using namespace drawborad;
TEST("C06 CMYK to PNG requires explicit RGB conversion consent"){ExportRequest r;r.format=ExportFormat::Png;r.path="x.png";r.size={10,10};CHECK(!preflightExport(ColorMode::CMYK,r));r.conversionConsent=true;auto p=preflightExport(ColorMode::CMYK,r);CHECK(p);CHECK(p.value().requiresRgbConversion);}
TEST("C06 JPEG alpha requires matte choice"){ExportRequest r;r.format=ExportFormat::Jpeg;r.path="x.jpg";r.size={10,10};r.alpha=true;CHECK(!preflightExport(ColorMode::RGB,r));r.matteChosen=true;CHECK(preflightExport(ColorMode::RGB,r));}

namespace {
TileData rgba8(){
    TileData p; p.width=2; p.height=2; p.format={ColorMode::RGB,Depth::U8,true}; p.rowStride=8;
    p.bytes={255,0,0,255, 0,255,0,128, 0,0,255,255, 255,255,255,0};
    return p;
}
TileData rgba16(){
    TileData p; p.width=1; p.height=1; p.format={ColorMode::RGB,Depth::U16,true}; p.rowStride=8;
    p.bytes={0xff,0xff,0,0,0,0,0xff,0xff};
    return p;
}
}
TEST("C06 real PNG JPEG WebP encoders emit their native signatures"){
    RasterEncodingOptions o; auto p=rgba8();
    auto png=encodeRaster(ExportFormat::Png,p,o); CHECK(png); CHECK(png.value().size()>8); CHECK(png.value()[0]==0x89); CHECK(png.value()[1]=='P');
    auto jpg=encodeRaster(ExportFormat::Jpeg,p,o); CHECK(jpg); CHECK(jpg.value().size()>4); CHECK(jpg.value()[0]==0xff); CHECK(jpg.value()[1]==0xd8);
    auto webp=encodeRaster(ExportFormat::Webp,p,o); CHECK(webp); CHECK(webp.value().size()>12); CHECK(std::string(webp.value().begin(),webp.value().begin()+4)=="RIFF");
}
TEST("C06 PNG16 and TIFF16 keep declared bit depth"){
    RasterEncodingOptions o; auto p=rgba16();
    auto png=encodeRaster(ExportFormat::Png,p,o); CHECK(png); CHECK(png.value()[24]==16);
    auto tiff=encodeRaster(ExportFormat::Tiff,p,o); CHECK(tiff); CHECK(tiff.value().size()>8);
    const bool little=tiff.value()[0]=='I'&&tiff.value()[1]=='I';
    const bool big=tiff.value()[0]=='M'&&tiff.value()[1]=='M';
    CHECK(little||big);
}
TEST("C06 writeRaster atomically publishes actual encoded bytes"){
    RasterEncodingOptions o; auto pixels=rgba8();
    auto path=std::filesystem::temp_directory_path()/"drawborad-export-real.png";
    ExportRequest r; r.format=ExportFormat::Png; r.path=path; r.size={2,2}; r.alpha=true;
    auto plan=preflightExport(ColorMode::RGB,r); CHECK(plan);
    auto report=writeRaster(plan.value(),pixels,o); CHECK(report);
    NativeFileIO io; auto bytes=io.read(path,1024*1024); CHECK(bytes); CHECK(bytes.value()[0]==0x89);
    CHECK(report.value().hashes[0]==sha256(bytes.value()));
    std::filesystem::remove(path);
}
