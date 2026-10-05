#include "Test.h"
#include "codecs/ImageReader.h"
#include "codecs/RasterWriters.h"
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
        CHECK(decoded.value().info.originalSize==PixelSize{2,2});
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
    CHECK(decoded.value().decodedPixels->format==PixelFormat{ColorMode::RGB,Depth::U8,false});
    std::filesystem::remove(path);
}
