#include "PsdReader.h"
#include "core/Document.h"
#include <cstring>
namespace drawborad { namespace {
std::uint16_t u16(const std::uint8_t*p){return std::uint16_t((p[0]<<8)|p[1]);}
std::uint32_t u32(const std::uint8_t*p){return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];}
std::uint64_t u64(const std::uint8_t*p){return (std::uint64_t(u32(p))<<32)|u32(p+4);}
struct R{std::span<const std::uint8_t>b;std::size_t p{};bool take(std::size_t n){if(n>b.size()-p)return false;p+=n;return true;}};
}
Result<PhotoshopInfo> readPhotoshopHeader(std::span<const std::uint8_t>b,const CodecLimits& lim){
 if(b.size()<26||std::memcmp(b.data(),"8BPS",4)!=0)return error(ErrorCode::CorruptData,"psd.signature");auto v=u16(b.data()+4);if(v!=1&&v!=2)return error(ErrorCode::UnsupportedFeature,"psd.version");PhotoshopInfo o;o.format=v==1?ImageFormat::Psd:ImageFormat::Psb;o.channels=u16(b.data()+12);o.size={u32(b.data()+18),u32(b.data()+14)};o.depth=u16(b.data()+22);o.colorMode=u16(b.data()+24);if(!o.size.width||!o.size.height||o.channels<1||o.channels>56||(o.depth!=8&&o.depth!=16))return error(ErrorCode::CorruptData,"psd.header-values");
 R r{b,26};for(int section=0;section<2;section++){if(!r.take(4))return error(ErrorCode::CorruptData,"psd.section-length");auto n=u32(b.data()+r.p-4);if(n>b.size()-r.p||!r.take(n))return error(ErrorCode::CorruptData,"psd.section-bounds");}
 auto lenBytes=v==1?4u:8u;if(!r.take(lenBytes))return error(ErrorCode::CorruptData,"psd.layer-mask-length");auto total=v==1?u32(b.data()+r.p-lenBytes):u64(b.data()+r.p-lenBytes);if(total>b.size()-r.p)return error(ErrorCode::CorruptData,"psd.layer-mask-bounds");if(total==0)return o;auto end=r.p+std::size_t(total);if(!r.take(lenBytes))return error(ErrorCode::CorruptData,"psd.layer-info-length");auto layerLen=v==1?u32(b.data()+r.p-lenBytes):u64(b.data()+r.p-lenBytes);if(layerLen>end-r.p)return error(ErrorCode::CorruptData,"psd.layer-info-bounds");if(layerLen==0)return o;if(!r.take(2))return error(ErrorCode::CorruptData,"psd.layer-count");auto count=std::int16_t(u16(b.data()+r.p-2));auto n=count<0?-count:count;if(std::uint64_t(n)>lim.nodes)return error(ErrorCode::BudgetExceeded,"psd.layer-count-limit");
 for(int k=0;k<n;k++){if(r.p+18>end)return error(ErrorCode::CorruptData,"psd.layer-record");PhotoshopLayerInfo li;li.top=std::int32_t(u32(b.data()+r.p));li.left=std::int32_t(u32(b.data()+r.p+4));li.bottom=std::int32_t(u32(b.data()+r.p+8));li.right=std::int32_t(u32(b.data()+r.p+12));r.p+=16;li.channels=u16(b.data()+r.p);r.p+=2;if(li.channels>56)return error(ErrorCode::CorruptData,"psd.channel-count");auto descriptor=v==1?6u:10u;if(std::uint64_t(li.channels)*descriptor>end-r.p)return error(ErrorCode::CorruptData,"psd.channel-records");r.p+=std::size_t(li.channels)*descriptor;if(r.p+16>end)return error(ErrorCode::CorruptData,"psd.layer-blend");if(std::memcmp(b.data()+r.p,"8BIM",4)!=0)return error(ErrorCode::CorruptData,"psd.blend-signature");r.p+=8;li.opacity=b[r.p];r.p+=4;auto extra=u32(b.data()+r.p);r.p+=4;if(extra>end-r.p)return error(ErrorCode::CorruptData,"psd.extra");r.p+=extra;o.layers.push_back(li);}
 return o;
}
}
