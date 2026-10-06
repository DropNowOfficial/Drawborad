#include "Test.h"
#include "codecs/ImageReader.h"
#include "codecs/HeicAdapter.h"
#include "codecs/RasterWriters.h"
#include "codecs/RasterReaders.h"
#include <filesystem>
#include <fstream>
using namespace drawborad;
namespace { std::filesystem::path put(std::string n,const Bytes&b){auto p=std::filesystem::temp_directory_path()/n;std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));return p;} }
TEST("C03 PNG preflight reads dimensions without decoding allocation"){Bytes b(26,0);const std::uint8_t s[8]={0x89,'P','N','G',13,10,26,10};std::copy(s,s+8,b.begin());b[12]='I';b[13]='H';b[14]='D';b[15]='R';b[19]=4;b[23]=3;b[24]=8;b[25]=6;auto p=put("drawborad-c03.png",b);ResourceBudget budget;auto i=preflightInput(p,budget);CHECK(i);CHECK(i.value().originalSize.width==4);CHECK(i.value().originalSize.height==3);std::filesystem::remove(p);}
TEST("C03 huge declared image is rejected by budget before decode"){Bytes b(24,0);const std::uint8_t s[8]={0x89,'P','N','G',13,10,26,10};std::copy(s,s+8,b.begin());b[12]='I';b[13]='H';b[14]='D';b[15]='R';b[16]=0x7f;b[17]=0xff;b[18]=0xff;b[19]=0xff;b[20]=0x7f;b[21]=0xff;b[22]=0xff;b[23]=0xff;auto p=put("drawborad-c03-huge.png",b);ResourceBudget budget;CHECK(!preflightInput(p,budget));std::filesystem::remove(p);}
TEST("C03 TIFF preflight reads bounded little-endian dimensions"){Bytes b(38,0);b[0]='I';b[1]='I';b[2]=42;b[4]=8;b[8]=2;b[9]=0;b[10]=0;b[11]=1;b[12]=4;b[13]=0;b[14]=1;b[18]=32;b[22]=1;b[23]=1;b[24]=4;b[25]=0;b[26]=1;b[30]=16;auto p=put("drawborad-c03.tif",b);ResourceBudget budget;auto i=preflightInput(p,budget);CHECK(i);CHECK(i.value().originalSize.width==32);CHECK(i.value().originalSize.height==16);std::filesystem::remove(p);}
TEST("C03 PNG alpha flag follows IHDR color type"){Bytes b(26,0);const std::uint8_t s[8]={0x89,'P','N','G',13,10,26,10};std::copy(s,s+8,b.begin());b[12]='I';b[13]='H';b[14]='D';b[15]='R';b[19]=2;b[23]=2;b[24]=8;b[25]=2;auto p=put("drawborad-c03-rgb.png",b);ResourceBudget budget;auto rgb=preflightInput(p,budget);CHECK(rgb);CHECK(!rgb.value().hasAlpha);b[25]=6;p=put("drawborad-c03-rgba.png",b);auto rgba=preflightInput(p,budget);CHECK(rgba);CHECK(rgba.value().hasAlpha);std::filesystem::remove(p);}

namespace {
TileData importFixture8(){
    TileData p; p.width=2; p.height=2; p.format={ColorMode::RGB,Depth::U8,true}; p.rowStride=8;
    p.bytes={10,20,30,255,40,50,60,255,70,80,90,255,100,110,120,128};
    return p;
}
std::filesystem::path writeEncoded(std::string name,const Bytes& bytes){
    auto p=std::filesystem::temp_directory_path()/name;
    std::ofstream f(p,std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    return p;
}
}
TEST("C03 native PNG WebP and TIFF decode to pixels under resource limits"){
    RasterEncodingOptions enc;
    auto source=importFixture8();
    for(auto format:{ExportFormat::Png,ExportFormat::Webp,ExportFormat::Tiff}){
        auto bytes=encodeRaster(format,source,enc); CHECK(bytes);
        std::string ext=format==ExportFormat::Png?".png":format==ExportFormat::Webp?".webp":".tif";
        auto path=writeEncoded("drawborad-c03-roundtrip"+ext,bytes.value());
        DecodeOptions options; options.limits.decodedBytes=1024*1024; options.limits.encodedBytes=1024*1024;
        auto decoded=decodeImage(path,options); CHECK(decoded); CHECK(decoded.value().decodedPixels.has_value());
        CHECK((decoded.value().info.originalSize==PixelSize{2,2}));
        CHECK(decoded.value().decodedPixels->width==2); CHECK(decoded.value().decodedPixels->height==2);
        CHECK(decoded.value().decodedPixels->format.mode==ColorMode::RGB);
        std::filesystem::remove(path);
    }
}
TEST("C03 JPEG decode returns RGB pixels and strips alpha through explicit encode matte"){
    RasterEncodingOptions enc; enc.matte={255,255,255};
    auto source=importFixture8();
    auto bytes=encodeRaster(ExportFormat::Jpeg,source,enc); CHECK(bytes);
    auto path=writeEncoded("drawborad-c03-roundtrip.jpg",bytes.value());
    DecodeOptions options; options.limits.decodedBytes=1024*1024; options.limits.encodedBytes=1024*1024;
    auto decoded=decodeImage(path,options); CHECK(decoded); CHECK(decoded.value().decodedPixels);
    CHECK((decoded.value().decodedPixels->format==PixelFormat{ColorMode::RGB,Depth::U8,false}));
    std::filesystem::remove(path);
}

namespace {
void le16(Bytes& b,std::size_t at,std::uint16_t v){
    if(b.size()<at+2)b.resize(at+2);
    b[at]=std::uint8_t(v);b[at+1]=std::uint8_t(v>>8);
}
void le32(Bytes& b,std::size_t at,std::uint32_t v){
    if(b.size()<at+4)b.resize(at+4);
    b[at]=std::uint8_t(v);b[at+1]=std::uint8_t(v>>8);
    b[at+2]=std::uint8_t(v>>16);b[at+3]=std::uint8_t(v>>24);
}
void tiffEntry(Bytes& b,std::size_t at,std::uint16_t tag,std::uint16_t type,
               std::uint32_t count,std::uint32_t value){
    le16(b,at,tag);le16(b,at+2,type);le32(b,at+4,count);le32(b,at+8,value);
}
void tiffPage(Bytes& b,std::size_t ifd,std::uint32_t bitsOffset,
              std::uint32_t pixelOffset,std::uint32_t nextIfd,
              std::array<std::uint8_t,3> pixel){
    le16(b,ifd,10);
    std::size_t e=ifd+2;
    tiffEntry(b,e+0*12,256,4,1,1);
    tiffEntry(b,e+1*12,257,4,1,1);
    tiffEntry(b,e+2*12,258,3,3,bitsOffset);
    tiffEntry(b,e+3*12,259,3,1,1);
    tiffEntry(b,e+4*12,262,3,1,2);
    tiffEntry(b,e+5*12,273,4,1,pixelOffset);
    tiffEntry(b,e+6*12,277,3,1,3);
    tiffEntry(b,e+7*12,278,4,1,1);
    tiffEntry(b,e+8*12,279,4,1,3);
    tiffEntry(b,e+9*12,284,3,1,1);
    le32(b,e+120,nextIfd);
    le16(b,bitsOffset,8);le16(b,bitsOffset+2,8);le16(b,bitsOffset+4,8);
    if(b.size()<pixelOffset+3)b.resize(pixelOffset+3);
    std::copy(pixel.begin(),pixel.end(),b.begin()+pixelOffset);
}
Bytes twoPageTiff(){
    Bytes b(279,0);
    b[0]='I';b[1]='I';le16(b,2,42);le32(b,4,8);
    tiffPage(b,8,134,140,144,{10,20,30});
    tiffPage(b,144,270,276,0,{200,150,100});
    return b;
}
}
TEST("C03 TIFF frame selection decodes requested page instead of silently using page zero"){
    auto path=writeEncoded("drawborad-c03-two-page.tif",twoPageTiff());
    DecodeOptions options;
    options.frame=1;
    options.limits.decodedBytes=1024*1024;
    options.limits.encodedBytes=1024*1024;
    auto decoded=decodeImage(path,options);
    CHECK(decoded);
    CHECK(decoded.value().decodedPixels);
    CHECK(decoded.value().decodedPixels->bytes.size()>=3);
    CHECK(decoded.value().decodedPixels->bytes[0]==200);
    CHECK(decoded.value().decodedPixels->bytes[1]==150);
    CHECK(decoded.value().decodedPixels->bytes[2]==100);
    std::filesystem::remove(path);
}

TEST("C03 HEIC detection is local and missing codec never pretends success"){
    Bytes heic(32,0);
    heic[0]=0;heic[1]=0;heic[2]=0;heic[3]=24;
    heic[4]='f';heic[5]='t';heic[6]='y';heic[7]='p';
    heic[8]='h';heic[9]='e';heic[10]='i';heic[11]='c';
    CHECK(looksLikeHeic(heic));
    DecodeOptions options;
    InputInfo info;
    info.format=ImageFormat::Heic;
    if(!heicCodecAvailable()){
        auto inspected=inspectHeic(heic,options.limits);
        CHECK(!inspected);
        CHECK(inspected.error().code==ErrorCode::UnsupportedFeature);
        auto decoded=decodeHeic(heic,info,options);
        CHECK(!decoded);
        CHECK(decoded.error().code==ErrorCode::UnsupportedFeature);
    } else {
        auto inspected=inspectHeic(heic,options.limits);
        CHECK(!inspected);
        CHECK(inspected.error().code==ErrorCode::CorruptData);
    }
}


TEST("C03 EXIF orientations one through eight map pixels deterministically"){
    auto make=[](){
        TileData p;
        p.width=2; p.height=3; p.format={ColorMode::RGB,Depth::U8,false}; p.rowStride=6;
        p.bytes={10,0,0, 20,0,0,
                 30,0,0, 40,0,0,
                 50,0,0, 60,0,0};
        return p;
    };
    const std::array<std::vector<std::uint8_t>,8> expected{{
        {10,20,30,40,50,60},
        {20,10,40,30,60,50},
        {60,50,40,30,20,10},
        {50,60,30,40,10,20},
        {10,30,50,20,40,60},
        {50,30,10,60,40,20},
        {60,40,20,50,30,10},
        {20,40,60,10,30,50}
    }};
    for(std::uint16_t orientation=1;orientation<=8;++orientation){
        auto pixels=make();
        auto result=applyExifOrientation(pixels,orientation);
        CHECK(result);
        CHECK(pixels.width==(orientation>=5?3u:2u));
        CHECK(pixels.height==(orientation>=5?2u:3u));
        std::vector<std::uint8_t> red;
        for(std::size_t i=0;i<pixels.bytes.size();i+=3)red.push_back(pixels.bytes[i]);
        CHECK(red==expected[orientation-1]);
    }
}


TEST("C03 JPEG import preserves embedded ICC bytes and reports GPS metadata") {
    RasterEncodingOptions options;
    auto source=importFixture8();
    source.format.alpha=false;
    source.rowStride=6;
    source.bytes={10,20,30,40,50,60,70,80,90,100,110,120};
    auto encoded=encodeRaster(ExportFormat::Jpeg,source,options);
    CHECK(encoded);

    Bytes icc(128,0);
    icc[36]='a';icc[37]='c';icc[38]='s';icc[39]='p';
    Bytes app2{0xff,0xe2};
    const std::uint16_t app2Length=std::uint16_t(16+icc.size());
    app2.push_back(std::uint8_t(app2Length>>8));
    app2.push_back(std::uint8_t(app2Length));
    const char signature[]="ICC_PROFILE";
    app2.insert(app2.end(),signature,signature+11);
    app2.push_back(0);
    app2.push_back(1);
    app2.push_back(1);
    app2.insert(app2.end(),icc.begin(),icc.end());

    Bytes exif{'E','x','i','f',0,0,'I','I',42,0,8,0,0,0,1,0};
    exif.insert(exif.end(),{0x25,0x88,4,0,1,0,0,0,26,0,0,0,0,0,0,0});
    Bytes app1{0xff,0xe1};
    const std::uint16_t app1Length=std::uint16_t(exif.size()+2);
    app1.push_back(std::uint8_t(app1Length>>8));
    app1.push_back(std::uint8_t(app1Length));
    app1.insert(app1.end(),exif.begin(),exif.end());

    Bytes jpeg;
    jpeg.insert(jpeg.end(),encoded.value().begin(),encoded.value().begin()+2);
    jpeg.insert(jpeg.end(),app2.begin(),app2.end());
    jpeg.insert(jpeg.end(),app1.begin(),app1.end());
    jpeg.insert(jpeg.end(),encoded.value().begin()+2,encoded.value().end());

    auto path=writeEncoded("drawborad-c03-icc-gps.jpg",jpeg);
    DecodeOptions decode;
    decode.limits.decodedBytes=1024*1024;
    decode.limits.encodedBytes=1024*1024;
    auto imported=decodeImage(path,decode);
    CHECK(imported);
    CHECK(imported.value().info.iccProfile==icc);
    CHECK(imported.value().info.icc==sha256(icc));
    CHECK(imported.value().info.hasGps);
    std::filesystem::remove(path);
}
