#include "Test.h"
#include "codecs/ExportService.h"
#include "codecs/RasterWriters.h"
#include "codecs/LayerArchive.h"
#include "storage/FileIO.h"
#include <fstream>
using namespace drawborad;
TEST("C06 CMYK to PNG requires consent and explicit RGB destination profile"){
    ExportRequest r;
    r.format=ExportFormat::Png;
    r.path="x.png";
    r.size={10,10};
    CHECK(!preflightExport(ColorMode::CMYK,r));
    r.conversionConsent=true;
    CHECK(!preflightExport(ColorMode::CMYK,r));
    r.profile.bytes[0]=1;
    auto p=preflightExport(ColorMode::CMYK,r);
    CHECK(p);
    CHECK(p.value().requiresRgbConversion);
}
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

namespace {
std::string firstZipName(const Bytes& b){
    if(b.size()<30||b[0]!='P'||b[1]!='K'||b[2]!=3||b[3]!=4)return {};
    auto n=std::uint16_t(b[26]|(std::uint16_t(b[27])<<8));
    if(30u+n>b.size())return {};
    return std::string(b.begin()+30,b.begin()+30+n);
}
std::string firstZipData(const Bytes& b){
    if(b.size()<30)return {};
    auto n=std::uint16_t(b[26]|(std::uint16_t(b[27])<<8));
    auto extra=std::uint16_t(b[28]|(std::uint16_t(b[29])<<8));
    auto size=std::uint32_t(b[18])|(std::uint32_t(b[19])<<8)|(std::uint32_t(b[20])<<16)|(std::uint32_t(b[21])<<24);
    auto start=30u+n+extra;
    if(start+size>b.size())return {};
    return std::string(b.begin()+start,b.begin()+start+size);
}
}
TEST("C06 OpenRaster starts with uncompressed mimetype and contains real PNG layers"){
    RasterEncodingOptions o; auto p=rgba8();
    ArchiveLayer layer; layer.name="图层 & one"; layer.pixels=p; layer.opacity=.5; layer.blend=BlendMode::Multiply;
    std::array<ArchiveLayer,1> layers{layer};
    auto ora=encodeOra({2,2},layers,p,o); CHECK(ora);
    CHECK(firstZipName(ora.value())=="mimetype");
    CHECK(firstZipData(ora.value())=="image/openraster");
    std::string all(ora.value().begin(),ora.value().end());
    CHECK(all.find("stack.xml")!=std::string::npos);
    CHECK(all.find("data/layer0.png")!=std::string::npos);
    CHECK(all.find("mergedimage.png")!=std::string::npos);
}
TEST("C06 layer ZIP carries a manifest and Unicode layer name"){
    RasterEncodingOptions o; auto p=rgba8();
    ArchiveLayer layer; layer.name="前景"; layer.pixels=p; layer.x=12; layer.y=-4;
    std::array<ArchiveLayer,1> layers{layer};
    auto z=encodeLayerZip({2,2},layers,o); CHECK(z);
    CHECK(firstZipName(z.value())=="manifest.json");
    auto manifest=firstZipData(z.value());
    CHECK(manifest.find("layers/0.png")!=std::string::npos);
    CHECK(manifest.find("12")!=std::string::npos);
}

TEST("C06 raster export never replaces existing file without overwrite consent"){
    RasterEncodingOptions options;
    auto pixels=rgba8();
    auto path=std::filesystem::temp_directory_path()/"drawborad-c06-no-clobber.png";
    const Bytes old{9,8,7,6};
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(old.data()),std::streamsize(old.size()));}
    ExportRequest request;
    request.format=ExportFormat::Png;
    request.path=path;
    request.size={2,2};
    request.alpha=true;
    request.overwrite=false;
    auto plan=preflightExport(ColorMode::RGB,request);
    CHECK(plan);
    auto result=writeRaster(plan.value(),pixels,options);
    CHECK(!result);
    NativeFileIO io;
    auto actual=io.read(path,1024);
    CHECK(actual);
    CHECK(actual.value()==old);
    std::filesystem::remove(path);
}
