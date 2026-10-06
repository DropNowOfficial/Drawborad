#include "ColorService.h"
#include "core/Commands.h"
#include <cstring>
#include <array>
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
Result<TileData> ColorService::proof(const TileData& in,const ColorTransform& documentToProof,
                                     const ColorTransform& proofToDisplay,bool paperWhite,
                                     bool gamutWarning)const{
 if(!paperWhite&&!gamutWarning){
  auto proofed=convert(in,documentToProof);if(!proofed)return proofed.error();
  return convert(proofed.value(),proofToDisplay);
 }
 if(in.profileId!=documentToProof.src||documentToProof.dst!=proofToDisplay.src)
  return error(ErrorCode::InvalidProfile,"icc.proof-chain");
 auto input=profiles_.get(documentToProof.src);if(!input)return input.error();
 auto proof=profiles_.get(documentToProof.dst);if(!proof)return proof.error();
 auto display=profiles_.get(proofToDisplay.dst);if(!display)return display.error();
 auto mode=[](IccColorSpace value)->std::optional<ColorMode>{
  if(value==IccColorSpace::RGB)return ColorMode::RGB;
  if(value==IccColorSpace::CMYK)return ColorMode::CMYK;
  return std::nullopt;
 };
 auto inputMode=mode(input.value().colorSpace),outputMode=mode(display.value().colorSpace);
 if(!inputMode||!outputMode||*inputMode!=in.format.mode)
  return error(ErrorCode::InvalidProfile,"icc.proof-space");
#ifndef DRAWBORAD_HAS_LCMS2
 return error(ErrorCode::UnsupportedFeature,"icc.proofing-unavailable");
#else
 cmsContext context=cmsCreateContext(nullptr,nullptr);
 if(!context)return error(ErrorCode::InvalidProfile,"icc.proof-context");
 auto closeContext=[&]{cmsDeleteContext(context);};
 auto ip=cmsOpenProfileFromMemTHR(context,input.value().bytes.data(),cmsUInt32Number(input.value().bytes.size()));
 auto pp=cmsOpenProfileFromMemTHR(context,proof.value().bytes.data(),cmsUInt32Number(proof.value().bytes.size()));
 auto dp=cmsOpenProfileFromMemTHR(context,display.value().bytes.data(),cmsUInt32Number(display.value().bytes.size()));
 if(!ip||!pp||!dp){if(ip)cmsCloseProfile(ip);if(pp)cmsCloseProfile(pp);if(dp)cmsCloseProfile(dp);closeContext();return error(ErrorCode::InvalidProfile,"icc.proof-open");}
 cmsUInt32Number flags=cmsFLAGS_SOFTPROOFING;
 if(documentToProof.blackPointCompensation||proofToDisplay.blackPointCompensation)
  flags|=cmsFLAGS_BLACKPOINTCOMPENSATION;
 if(gamutWarning){
  flags|=cmsFLAGS_GAMUTCHECK;
  std::array<cmsUInt16Number,cmsMAXCHANNELS> alarm{};
  alarm[0]=0xffff;alarm[1]=0;alarm[2]=0xffff;
  cmsSetAlarmCodesTHR(context,alarm.data());
 }
 const auto proofIntent=paperWhite?INTENT_ABSOLUTE_COLORIMETRIC:iv(documentToProof.intent);
 auto transform=cmsCreateProofingTransformTHR(context,ip,pt(*inputMode,in.format.depth),
                  dp,pt(*outputMode,in.format.depth),pp,iv(proofToDisplay.intent),
                  proofIntent,flags);
 if(!transform){cmsCloseProfile(ip);cmsCloseProfile(pp);cmsCloseProfile(dp);closeContext();return error(ErrorCode::InvalidProfile,"icc.proof-transform");}
 const unsigned bytesPer=in.format.depth==Depth::U16?2u:1u;
 const unsigned srcColor=chans(*inputMode),dstColor=chans(*outputMode);
 const unsigned srcChannels=srcColor+(in.format.alpha?1u:0u);
 const unsigned dstChannels=dstColor+(in.format.alpha?1u:0u);
 TileData out;out.width=in.width;out.height=in.height;
 out.format={*outputMode,in.format.depth,in.format.alpha};out.profileId=proofToDisplay.dst;
 out.engineVersion=std::max(documentToProof.engineVersion,proofToDisplay.engineVersion);
 out.rowStride=std::uint64_t(out.width)*dstChannels*bytesPer;
 auto total=checkedMultiply(out.rowStride,out.height);
 if(!total||total.value()>SIZE_MAX){cmsDeleteTransform(transform);cmsCloseProfile(ip);cmsCloseProfile(pp);cmsCloseProfile(dp);closeContext();return error(ErrorCode::BudgetExceeded,"icc.proof-size");}
 try{out.bytes.resize(std::size_t(total.value()));}catch(const std::bad_alloc&){cmsDeleteTransform(transform);cmsCloseProfile(ip);cmsCloseProfile(pp);cmsCloseProfile(dp);closeContext();return error(ErrorCode::BudgetExceeded,"icc.proof-allocation");}
 Bytes src(std::size_t(in.width)*srcColor*bytesPer),dst(std::size_t(in.width)*dstColor*bytesPer);
 for(std::uint32_t y=0;y<in.height;y++){
  const auto* sr=in.bytes.data()+std::size_t(y*in.rowStride);
  auto* dr=out.bytes.data()+std::size_t(y*out.rowStride);
  for(std::uint32_t x=0;x<in.width;x++)
   std::memcpy(src.data()+std::size_t(x*srcColor*bytesPer),
               sr+std::size_t(x*srcChannels*bytesPer),srcColor*bytesPer);
  cmsDoTransform(transform,src.data(),dst.data(),in.width);
  for(std::uint32_t x=0;x<in.width;x++){
   std::memcpy(dr+std::size_t(x*dstChannels*bytesPer),
               dst.data()+std::size_t(x*dstColor*bytesPer),dstColor*bytesPer);
   if(in.format.alpha)
    std::memcpy(dr+std::size_t(x*dstChannels*bytesPer+dstColor*bytesPer),
                sr+std::size_t(x*srcChannels*bytesPer+srcColor*bytesPer),bytesPer);
  }
 }
 cmsDeleteTransform(transform);cmsCloseProfile(ip);cmsCloseProfile(pp);cmsCloseProfile(dp);closeContext();
 return out;
#endif
}

}