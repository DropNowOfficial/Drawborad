#include "ColorService.h"
#include "core/Commands.h"
#include <cstring>
#ifdef DRAWBORAD_HAS_LCMS2
#include <lcms2.h>
#endif
namespace drawborad { namespace {
IccColorSpace space(ColorMode m){return m==ColorMode::RGB?IccColorSpace::RGB:IccColorSpace::CMYK;}
#ifdef DRAWBORAD_HAS_LCMS2
unsigned chans(ColorMode m){return m==ColorMode::RGB?3u:4u;}
#endif
#ifdef DRAWBORAD_HAS_LCMS2
int iv(ColorIntent i){switch(i){case ColorIntent::Perceptual:return INTENT_PERCEPTUAL;case ColorIntent::Relative:return INTENT_RELATIVE_COLORIMETRIC;case ColorIntent::Absolute:return INTENT_ABSOLUTE_COLORIMETRIC;case ColorIntent::Saturation:return INTENT_SATURATION;}return INTENT_RELATIVE_COLORIMETRIC;}
cmsUInt32Number pt(ColorMode m,Depth d){if(m==ColorMode::RGB)return d==Depth::U8?TYPE_RGB_8:TYPE_RGB_16;return d==Depth::U8?TYPE_CMYK_8:TYPE_CMYK_16;}
#endif
}
Result<ColorSpec> ColorService::assign(const ColorSpec&c,ProfileId id)const{auto p=profiles_.get(id);if(!p)return p.error();if(p.value().colorSpace!=space(c.format.mode))return error(ErrorCode::InvalidProfile,"icc.channel-mismatch");auto o=c;o.profileId=id;return o;}
Result<ChangeSet> ColorService::assign(Document& document,ProfileId id)const{
 auto before=document.snapshot();
 auto target=assign(before.spec.colorSpec,id);
 if(!target)return target.error();
 auto events=std::make_shared<EventList>(*before.eventIndex);
 for(auto& event:*events)event.color.profileId=id;
 std::map<NodeId,std::vector<Hash>> sameContent;
 for(const auto& [nodeId,node]:*before.nodes)
     if(!node.content.empty())sameContent.emplace(nodeId,node.content);
 std::optional<ColorValue> paper;
 if(!before.spec.background.transparent){
     paper=before.spec.background.paperColor;
     paper->profileId=id;
 }
 ConvertColor operation{target.value(),std::move(sameContent),
                        std::make_shared<const EventList>(*events),paper};
 return document.apply(Command{std::move(operation)},before.revision);
}
bool ColorService::hasNativeTransformBackend()const{
#ifdef DRAWBORAD_HAS_LCMS2
return true;
#else
return false;
#endif
}
Result<TileData> ColorService::convert(const TileData&in,const ColorTransform&t)const{
 if(in.profileId!=t.src)return error(ErrorCode::InvalidProfile,"icc.source-mismatch");
 auto s=profiles_.get(t.src);
 if(!s)return s.error();
 auto d=profiles_.get(t.dst);
 if(!d)return d.error();
 ColorMode sm=s.value().colorSpace==IccColorSpace::CMYK?ColorMode::CMYK:ColorMode::RGB;
 if(sm!=in.format.mode)return error(ErrorCode::InvalidProfile,"icc.input-space-mismatch");
 if(t.src==t.dst){auto o=in;o.profileId=t.dst;return o;}
#ifndef DRAWBORAD_HAS_LCMS2
 return error(ErrorCode::UnsupportedFeature,"icc.lcms2-unavailable");
#else
 ColorMode dm=d.value().colorSpace==IccColorSpace::CMYK?ColorMode::CMYK:ColorMode::RGB;
 auto sp=cmsOpenProfileFromMem(s.value().bytes.data(),cmsUInt32Number(s.value().bytes.size()));auto dp=cmsOpenProfileFromMem(d.value().bytes.data(),cmsUInt32Number(d.value().bytes.size()));if(!sp||!dp){if(sp)cmsCloseProfile(sp);if(dp)cmsCloseProfile(dp);return error(ErrorCode::InvalidProfile,"icc.open");}
 auto x=cmsCreateTransform(sp,pt(sm,in.format.depth),dp,pt(dm,in.format.depth),iv(t.intent),t.blackPointCompensation?cmsFLAGS_BLACKPOINTCOMPENSATION:0);if(!x){cmsCloseProfile(sp);cmsCloseProfile(dp);return error(ErrorCode::InvalidProfile,"icc.transform");}
 unsigned sb=in.format.depth==Depth::U16?2u:1u,sc=chans(sm),dc=chans(dm);
 const unsigned srcChannels=sc+(in.format.alpha?1u:0u);
 const unsigned dstChannels=dc+(in.format.alpha?1u:0u);
 TileData o;o.width=in.width;o.height=in.height;o.format={dm,in.format.depth,in.format.alpha};o.profileId=t.dst;o.engineVersion=t.engineVersion;o.rowStride=std::uint64_t(o.width)*dstChannels*sb;auto total=checkedMultiply(o.rowStride,o.height);if(!total||total.value()>SIZE_MAX){cmsDeleteTransform(x);cmsCloseProfile(sp);cmsCloseProfile(dp);return error(ErrorCode::BudgetExceeded,"icc.output-size");}try{o.bytes.resize(std::size_t(total.value()));}catch(const std::bad_alloc&){cmsDeleteTransform(x);cmsCloseProfile(sp);cmsCloseProfile(dp);return error(ErrorCode::BudgetExceeded,"icc.output-allocation");}
 Bytes a(std::size_t(in.width)*sc*sb),b(std::size_t(in.width)*dc*sb);for(std::uint32_t y=0;y<in.height;y++){auto*sr=in.bytes.data()+std::size_t(y*in.rowStride);auto*dr=o.bytes.data()+std::size_t(y*o.rowStride);for(std::uint32_t px=0;px<in.width;px++)std::memcpy(a.data()+std::size_t(px*sc*sb),sr+std::size_t(px*srcChannels*sb),sc*sb);cmsDoTransform(x,a.data(),b.data(),in.width);for(std::uint32_t px=0;px<in.width;px++){std::memcpy(dr+std::size_t(px*dstChannels*sb),b.data()+std::size_t(px*dc*sb),dc*sb);if(in.format.alpha)std::memcpy(dr+std::size_t(px*dstChannels*sb+dc*sb),sr+std::size_t(px*srcChannels*sb+sc*sb),sb);}}
 cmsDeleteTransform(x);cmsCloseProfile(sp);cmsCloseProfile(dp);return o;
#endif
}
}