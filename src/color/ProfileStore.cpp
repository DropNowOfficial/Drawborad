#include "ProfileStore.h"
#include <cstring>
namespace drawborad { namespace {
std::uint32_t be32(const std::uint8_t*p){return(std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];}
bool tag(std::span<const std::uint8_t>b,std::size_t o,const char*t){return o+4<=b.size()&&std::memcmp(b.data()+o,t,4)==0;}
}
Result<ProfileInfo> inspectIcc(std::span<const std::uint8_t>b){
 if(b.size()<128||b.size()>32ull*1024*1024)return error(ErrorCode::InvalidProfile,"icc.size");
 auto n=be32(b.data());if(n<128||n>b.size())return error(ErrorCode::InvalidProfile,"icc.declared-size");
 if(!tag(b,36,"acsp"))return error(ErrorCode::InvalidProfile,"icc.signature");
 IccColorSpace s=IccColorSpace::Unknown;if(tag(b,16,"RGB "))s=IccColorSpace::RGB;else if(tag(b,16,"CMYK"))s=IccColorSpace::CMYK;else if(tag(b,16,"GRAY"))s=IccColorSpace::Gray;else return error(ErrorCode::InvalidProfile,"icc.color-space");
 ProfileInfo i;i.id=sha256(b.first(n));i.colorSpace=s;i.declaredSize=n;i.bytes.assign(b.begin(),b.begin()+n);return i;
}
Result<ProfileId> ProfileStore::import(std::span<const std::uint8_t>b){auto i=inspectIcc(b);if(!i)return i.error();std::lock_guard l(mutex_);profiles_.insert_or_assign(i.value().id,i.value());return i.value().id;}
Result<ProfileInfo> ProfileStore::get(ProfileId id)const{std::lock_guard l(mutex_);auto i=profiles_.find(id);if(i==profiles_.end())return error(ErrorCode::InvalidProfile,"icc.not-found");return i->second;}
bool ProfileStore::contains(ProfileId id)const{std::lock_guard l(mutex_);return profiles_.contains(id);}std::size_t ProfileStore::size()const{std::lock_guard l(mutex_);return profiles_.size();}
}