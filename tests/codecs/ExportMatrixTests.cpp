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

TEST("C06 OpenRaster includes required bounded thumbnail"){
    RasterEncodingOptions options;
    auto pixels=rgba8();
    ArchiveLayer layer;
    layer.name="thumb";
    layer.pixels=pixels;
    std::array<ArchiveLayer,1> layers{layer};
    auto ora=encodeOra({2,2},layers,pixels,options);
    CHECK(ora);
    std::string all(ora.value().begin(),ora.value().end());
    CHECK(all.find("Thumbnails/thumbnail.png")!=std::string::npos);
}

TEST("C06 OpenRaster thumbnail is 8 bit and bounded to 256 pixels"){
    RasterEncodingOptions options;
    TileData pixels;
    pixels.width=300; pixels.height=100;
    pixels.format={ColorMode::RGB,Depth::U16,true};
    pixels.rowStride=300*8;
    pixels.bytes.assign(std::size_t(pixels.rowStride)*pixels.height,0xff);
    ArchiveLayer layer; layer.name="large"; layer.pixels=pixels;
    std::array<ArchiveLayer,1> layers{layer};
    auto ora=encodeOra({300,100},layers,pixels,options);
    CHECK(ora);
    const std::string name="Thumbnails/thumbnail.png";
    std::string all(ora.value().begin(),ora.value().end());
    auto nameAt=all.find(name);
    CHECK(nameAt!=std::string::npos);
    CHECK(nameAt>=30);
    const auto header=nameAt-30;
    const auto dataAt=nameAt+name.size();
    CHECK(dataAt+25<=ora.value().size());
    const auto& b=ora.value();
    CHECK(b[header]=='P'&&b[header+1]=='K'&&b[header+2]==3&&b[header+3]==4);
    CHECK(b[dataAt]==0x89&&b[dataAt+1]=='P'&&b[dataAt+2]=='N'&&b[dataAt+3]=='G');
    auto be32=[&](std::size_t at){return (std::uint32_t(b[at])<<24)|(std::uint32_t(b[at+1])<<16)|(std::uint32_t(b[at+2])<<8)|b[at+3];};
    CHECK(be32(dataAt+16)<=256);
    CHECK(be32(dataAt+20)<=256);
    CHECK(b[dataAt+24]==8);
}

TEST("C07 legacy export entry cannot clobber existing destination"){
    auto path=std::filesystem::temp_directory_path()/"drawborad-c07-placeholder-no-clobber.bin";
    const Bytes old{1,2,3,4};
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(old.data()),std::streamsize(old.size()));}
    ExportRequest request;
    request.format=ExportFormat::Png;
    request.path=path;
    request.size={1,1};
    request.overwrite=false;
    auto plan=preflightExport(ColorMode::RGB,request);
    CHECK(plan);
    const Bytes next{9,9,9};
    auto result=writeRasterPlaceholder(plan.value(),next);
    CHECK(!result);
    NativeFileIO io;
    auto actual=io.read(path,1024);
    CHECK(actual);
    CHECK(actual.value()==old);
    std::filesystem::remove(path);
}


TEST("C06 ExportService routes a real PNG through atomic publication"){
    auto path=std::filesystem::temp_directory_path()/"drawborad-c06-service.png";
    std::filesystem::remove(path);
    ExportRequest request;
    request.format=ExportFormat::Png;
    request.path=path;
    request.size={2,1};
    request.alpha=true;
    request.overwrite=false;
    auto plan=preflightExport(ColorMode::RGB,request);
    CHECK(plan);
    TileData merged;
    merged.width=2; merged.height=1;
    merged.format={ColorMode::RGB,Depth::U8,true};
    merged.rowStride=8;
    merged.bytes={255,0,0,255,0,255,0,255};
    RasterEncodingOptions options;
    ExportService service;
    auto report=service.write(plan.value(),merged,{},options);
    CHECK(report);
    CHECK(report.value().paths.size()==1);
    std::array<unsigned char,8> signature{};
    {
        std::ifstream in(path,std::ios::binary);
        in.read(reinterpret_cast<char*>(signature.data()),8);
        CHECK(in.good());
    }
    CHECK(signature==std::array<unsigned char,8>{0x89,'P','N','G',13,10,26,10});
    std::filesystem::remove(path);
}


TEST("C06 CMYKA16 TIFF roundtrip preserves five native channels"){
    TileData pixels;
    pixels.width=1; pixels.height=1;
    pixels.format={ColorMode::CMYK,Depth::U16,true};
    pixels.rowStride=10;
    pixels.bytes={0x34,0x12, 0x78,0x56, 0xbc,0x9a, 0xff,0xde, 0x11,0x80};
    RasterEncodingOptions options;
    auto encoded=encodeRaster(ExportFormat::Tiff,pixels,options);
    CHECK(encoded);
    auto path=std::filesystem::temp_directory_path()/"drawborad-c06-cmyka16.tif";
    {std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(encoded.value().data()),std::streamsize(encoded.value().size()));}
    DecodeOptions decode;
    decode.limits.encodedBytes=1024*1024;
    decode.limits.decodedBytes=1024*1024;
    auto image=decodeImage(path,decode);
    CHECK(image);
    CHECK(image.value().decodedPixels);
    CHECK(image.value().decodedPixels->format==PixelFormat{ColorMode::CMYK,Depth::U16,true});
    CHECK(image.value().decodedPixels->bytes==pixels.bytes);
    std::filesystem::remove(path);
}


TEST("C06 metadata is private by default and XMP preservation is explicit") {
    auto pixels=rgba8();
    RasterEncodingOptions options;
    const std::string marker="DRAWBORAD-XMP-SENTINEL";
    options.metadata.xmp.assign(marker.begin(),marker.end());
    auto stripped=encodeRaster(ExportFormat::Jpeg,pixels,options);
    CHECK(stripped);
    std::string strippedText(stripped.value().begin(),stripped.value().end());
    CHECK(strippedText.find(marker)==std::string::npos);

    options.metadata.preserveXmp=true;
    for(auto format:{ExportFormat::Png,ExportFormat::Jpeg,ExportFormat::Webp}) {
        auto encoded=encodeRaster(format,pixels,options);
        CHECK(encoded);
        std::string bytes(encoded.value().begin(),encoded.value().end());
        CHECK(bytes.find(marker)!=std::string::npos);
    }
}

TEST("C06 GPS-bearing EXIF requires separate explicit consent") {
    auto pixels=rgba8();
    RasterEncodingOptions options;
    const char exifPrefix[]={'E','x','i','f',0,0};
    options.metadata.exif.assign(exifPrefix,exifPrefix+sizeof(exifPrefix));
    options.metadata.exifContainsGps=true;
    options.metadata.preserveExif=true;
    auto blocked=encodeRaster(ExportFormat::Jpeg,pixels,options);
    CHECK(!blocked);
    CHECK(blocked.error().code==ErrorCode::ConversionConsentRequired);
    options.metadata.allowGps=true;
    auto allowed=encodeRaster(ExportFormat::Jpeg,pixels,options);
    CHECK(allowed);
}


#include "RenderFixture.h"
TEST("C06 document preflight binds export dimensions and document color mode"){
    auto s=fixture::snapshot(ColorMode::CMYK,Depth::U16,32,16);
    ExportRequest r;
    r.format=ExportFormat::Png;
    r.path="x.png";
    r.size=s.spec.pixelSize;
    r.depth=Depth::U16;
    r.conversionConsent=true;
    r.profile.bytes[0]=2;
    auto plan=preflightExport(s,r);
    CHECK(plan);
    CHECK(plan.value().request.size==s.spec.pixelSize);
    CHECK(plan.value().requiresRgbConversion);
    r.size={31,16};
    CHECK(!preflightExport(s,r));
}
