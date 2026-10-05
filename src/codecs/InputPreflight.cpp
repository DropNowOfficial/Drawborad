#include "ImageReader.h"
#include "core/Digest.h"
#include <fstream>
#include <array>
#include <cstring>
namespace drawborad { namespace {
std::uint16_t le16(const std::uint8_t*p){return std::uint16_t(p[0]|(p[1]<<8));}std::uint32_t le32(const std::uint8_t*p){return std::uint32_t(p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24));}std::uint32_t be32(const std::uint8_t*p){return(std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];}
Result<Bytes> head(const InputPath&p,std::size_t n){std::ifstream f(p,std::ios::binary);if(!f)return error(ErrorCode::IoError,"codec.open");Bytes b(n);f.read(reinterpret_cast<char*>(b.data()),std::streamsize(n));b.resize(std::size_t(f.gcount()));return b;}
Result<PixelSize> jpegSize(const InputPath&p){std::ifstream f(p,std::ios::binary);if(!f)return error(ErrorCode::IoError,"codec.open");std::uint8_t a=0,b=0;f.read((char*)&a,1);f.read((char*)&b,1);if(a!=0xff||b!=0xd8)return error(ErrorCode::CorruptData,"jpeg.signature");while(f){f.read((char*)&a,1);if(a!=0xff)continue;do{f.read((char*)&b,1);}while(b==0xff);if(b==0xd9||b==0xda)break;std::uint8_t l[2];f.read((char*)l,2);auto n=std::uint16_t((l[0]<<8)|l[1]);if(n<2)return error(ErrorCode::CorruptData,"jpeg.segment");if((b>=0xc0&&b<=0xc3)||(b>=0xc5&&b<=0xc7)||(b>=0xc9&&b<=0xcb)||(b>=0xcd&&b<=0xcf)){std::uint8_t s[5];f.read((char*)s,5);if(!f)return error(ErrorCode::CorruptData,"jpeg.sof");return PixelSize{std::uint64_t((s[3]<<8)|s[4]),std::uint64_t((s[1]<<8)|s[2])};}f.seekg(n-2,std::ios::cur);}return error(ErrorCode::CorruptData,"jpeg.no-sof");}
}
Result<InputInfo> preflightInput(const InputPath&p,const ResourceBudget&budget){std::error_code ec;auto sz=std::filesystem::file_size(p,ec);if(ec||sz<8)return error(ErrorCode::IoError,"codec.file");auto h=head(p,64);if(!h)return h.error();auto&b=h.value();InputInfo i;PixelSize px{};
 if(b.size()>=24&&std::memcmp(b.data(),"\x89PNG\r\n\x1a\n",8)==0){i.format=ImageFormat::PNG;px={be32(b.data()+16),be32(b.data()+20)};}
 else if(b.size()>=2&&b[0]==0xff&&b[1]==0xd8){i.format=ImageFormat::JPEG;auto q=jpegSize(p);if(!q)return q.error();px=q.value();}
 else if(b.size()>=30&&std::memcmp(b.data(),"RIFF",4)==0&&std::memcmp(b.data()+8,"WEBP",4)==0){i.format=ImageFormat::WebP;if(std::memcmp(b.data()+12,"VP8X",4)!=0)return error(ErrorCode::UnsupportedFeature,"webp.preflight.variant");px={1ull+b[24]+(std::uint64_t(b[25])<<8)+(std::uint64_t(b[26])<<16),1ull+b[27]+(std::uint64_t(b[28])<<8)+(std::uint64_t(b[29])<<16)};}
 else if(b.size()>=26&&b[0]=='B'&&b[1]=='M'){i.format=ImageFormat::BMP;px={le32(b.data()+18),le32(b.data()+22)};}
 else if(b.size()>=8&&((b[0]=='I'&&b[1]=='I'&&le16(b.data()+2)==42)||(b[0]=='M'&&b[1]=='M'&&b[2]==0&&b[3]==42))){i.format=ImageFormat::TIFF;return error(ErrorCode::UnsupportedFeature,"tiff.preflight.requires-backend");}
 else if(b.size()>=8&&std::memcmp(b.data(),"8BPS",4)==0){auto v=(b[4]<<8)|b[5];i.format=v==2?ImageFormat::PSB:ImageFormat::PSD;if(v!=1&&v!=2)return error(ErrorCode::CorruptData,"psd.version");if(b.size()<26)return error(ErrorCode::CorruptData,"psd.header");px={be32(b.data()+18),be32(b.data()+14)};}
 else return error(ErrorCode::UnsupportedFeature,"codec.signature");
 if(!px.width||!px.height)return error(ErrorCode::CorruptData,"codec.dimensions");auto pixels=checkedMultiply(px.width,px.height);if(!pixels)return pixels.error();auto bytes=checkedMultiply(pixels.value(),10);if(!bytes)return bytes.error();if(bytes.value()>budget.ramReclaimBytes||bytes.value()>budget.scratchBytes)return error(ErrorCode::BudgetExceeded,"codec.preflight-budget");
 i.originalSize=i.orientedSize=px;i.estimatedBytes=bytes.value();std::ifstream all(p,std::ios::binary);Bytes data((std::istreambuf_iterator<char>(all)),{});i.sourceHash=sha256(data);return i;}
}