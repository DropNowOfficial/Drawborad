#include "ImageReader.h"
#include "HeicAdapter.h"
#include "storage/FileIO.h"
#include "RasterReaders.h"
#include <algorithm>
#include <array>
#include <cstring>
#ifdef DRAWBORAD_HAS_WEBP
#include <webp/decode.h>
#endif
namespace drawborad { namespace {
std::uint16_t be16(const std::uint8_t* p){return std::uint16_t((p[0]<<8)|p[1]);}
std::uint32_t be32(const std::uint8_t* p){return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];}
std::uint16_t le16(const std::uint8_t* p){return std::uint16_t(p[0]|(std::uint16_t(p[1])<<8));}
std::uint32_t le32(const std::uint8_t* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
std::uint16_t t16(const std::uint8_t* p,bool little){return little?le16(p):be16(p);}
std::uint32_t t32(const std::uint8_t* p,bool little){return little?le32(p):be32(p);}
bool sig(std::span<const std::uint8_t>b,std::size_t o,const char*s,std::size_t n){return o+n<=b.size()&&std::memcmp(b.data()+o,s,n)==0;}
Result<InputInfo> inspect(std::span<const std::uint8_t>b){
 if(b.size()<12)return error(ErrorCode::CorruptData,"codec.short");
 InputInfo i;i.sourceHash=sha256(b);
 if(sig(b,0,"\x89PNG\r\n\x1a\n",8)){if(b.size()<26||!sig(b,12,"IHDR",4))return error(ErrorCode::CorruptData,"png.header");i.format=ImageFormat::Png;i.originalSize={be32(b.data()+16),be32(b.data()+20)};auto colorType=b[25];if(colorType!=0&&colorType!=2&&colorType!=3&&colorType!=4&&colorType!=6)return error(ErrorCode::CorruptData,"png.color-type");i.hasAlpha=colorType==4||colorType==6;}
 else if(b[0]==0xff&&b[1]==0xd8){i.format=ImageFormat::Jpeg;std::size_t p=2;bool found=false;while(p+4<=b.size()){if(b[p]!=0xff){++p;continue;}auto m=b[p+1];p+=2;if(m==0xd9||m==0xda)break;if(p+2>b.size())break;auto n=be16(b.data()+p);if(n<2||p+n>b.size())return error(ErrorCode::CorruptData,"jpeg.segment");if((m>=0xc0&&m<=0xc3)&&n>=7){i.originalSize={be16(b.data()+p+5),be16(b.data()+p+3)};found=true;break;}p+=n;}if(!found)return error(ErrorCode::CorruptData,"jpeg.dimensions");}
 else if(sig(b,0,"RIFF",4)&&sig(b,8,"WEBP",4)){
  i.format=ImageFormat::Webp;
#ifdef DRAWBORAD_HAS_WEBP
  WebPBitstreamFeatures features{};
  if(WebPGetFeatures(b.data(),b.size(),&features)!=VP8_STATUS_OK)
      return error(ErrorCode::CorruptData,"webp.header");
  if(features.width<=0||features.height<=0)
      return error(ErrorCode::CorruptData,"webp.dimensions");
  i.originalSize={std::uint64_t(features.width),std::uint64_t(features.height)};
  i.hasAlpha=features.has_alpha!=0;
#else
  if(b.size()>=30&&sig(b,12,"VP8X",4)){
      auto w=1u+std::uint32_t(b[24])+(std::uint32_t(b[25])<<8)+(std::uint32_t(b[26])<<16);
      auto h=1u+std::uint32_t(b[27])+(std::uint32_t(b[28])<<8)+(std::uint32_t(b[29])<<16);
      i.originalSize={w,h};i.hasAlpha=(b[20]&0x10)!=0;
  }else return error(ErrorCode::UnsupportedFeature,"webp.header-parser-unavailable");
#endif
 }
 else if(sig(b,0,"BM",2)){if(b.size()<26)return error(ErrorCode::CorruptData,"bmp.header");i.format=ImageFormat::Bmp;i.originalSize={le32(b.data()+18),std::uint64_t(std::abs(std::int32_t(le32(b.data()+22))))};}
 else if(sig(b,0,"8BPS",4)){if(b.size()<26)return error(ErrorCode::CorruptData,"psd.header");auto v=be16(b.data()+4);i.format=v==1?ImageFormat::Psd:v==2?ImageFormat::Psb:ImageFormat::Unknown;if(i.format==ImageFormat::Unknown)return error(ErrorCode::UnsupportedFeature,"psd.version");i.originalSize={be32(b.data()+18),be32(b.data()+14)};}
 else if(sig(b,0,"II*\0",4)||sig(b,0,"MM\0*",4)){
  const bool little=sig(b,0,"II*\0",4);i.format=ImageFormat::Tiff;
  if(b.size()<8)return error(ErrorCode::CorruptData,"tiff.header");
  auto ifd=t32(b.data()+4,little);
  if(ifd>b.size()-2)return error(ErrorCode::CorruptData,"tiff.ifd-offset");
  auto count=t16(b.data()+ifd,little);
  auto p=std::size_t(ifd)+2;
  std::uint64_t width=0,height=0;
  if(std::uint64_t(count)*12u>b.size()-p)return error(ErrorCode::CorruptData,"tiff.ifd-bounds");
  for(std::uint16_t n=0;n<count;n++,p+=12){
    const std::uint16_t tag=t16(b.data()+p,little);
    const std::uint16_t type=t16(b.data()+p+2,little);
    const std::uint32_t items=t32(b.data()+p+4,little);
    if(items!=1)continue;
    std::uint64_t value=0;
    if(type==3)value=t16(b.data()+p+8,little);
    else if(type==4)value=t32(b.data()+p+8,little);
    else continue;
    if(tag==256)width=value;else if(tag==257)height=value;
  }
  i.originalSize={width,height};
 }
 else if(looksLikeHeic(b)){
  auto heic=inspectHeic(b,CodecLimits{});
  if(!heic)return heic.error();
  i=heic.value();
 }
 else return error(ErrorCode::UnsupportedFeature,"codec.magic");
 if(!i.originalSize.width||!i.originalSize.height)return error(ErrorCode::CorruptData,"codec.zero-size");
 auto pixels=checkedMultiply(i.originalSize.width,i.originalSize.height);if(!pixels)return pixels.error();auto est=checkedMultiply(pixels.value(),8);if(!est)return est.error();i.estimatedBytes=est.value();i.orientedSize=i.originalSize;return i;
}
}
Result<InputInfo> preflightInput(const InputPath& p,const ResourceBudget& budget,std::uint64_t maxEncodedBytes){NativeFileIO io;auto b=io.read(p,std::min<std::uint64_t>(budget.scratchBytes,maxEncodedBytes));if(!b)return b.error();auto i=inspect(b.value());if(!i)return i.error();if(i.value().estimatedBytes>budget.ramReclaimBytes)return error(ErrorCode::BudgetExceeded,"codec.preflight-budget");return i;}
Result<ImportedImage> decodeImage(const InputPath& p,const DecodeOptions& o,std::stop_token st){
 if(st.stop_requested())return error(ErrorCode::Cancelled,"codec.cancelled");
 NativeFileIO io;
 auto b=io.read(p,o.limits.encodedBytes);if(!b)return b.error();
 auto i=inspect(b.value());if(!i)return i.error();
 if(i.value().estimatedBytes>o.limits.decodedBytes)return error(ErrorCode::BudgetExceeded,"codec.decode-budget");
 if(st.stop_requested())return error(ErrorCode::Cancelled,"codec.cancelled");
 ImportedImage out{i.value(),b.value(),std::nullopt};
 if(out.info.format==ImageFormat::Png||out.info.format==ImageFormat::Jpeg||
    out.info.format==ImageFormat::Webp||out.info.format==ImageFormat::Tiff||
    out.info.format==ImageFormat::Bmp){
   auto decoded=decodeRasterBytes(out.encodedSource,out.info,o);
   if(!decoded)return decoded.error();
   out.decodedPixels=std::move(decoded.value());
 } else if(out.info.format==ImageFormat::Heic) {
   auto decoded=decodeHeic(out.encodedSource,out.info,o,st);
   if(!decoded)return decoded.error();
   out.decodedPixels=std::move(decoded.value());
 }
 return out;
}
}
