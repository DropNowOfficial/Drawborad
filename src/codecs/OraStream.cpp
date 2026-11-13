#include "OraStream.h"
#include "AtomicExport.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <locale>
#include <sstream>
namespace drawborad {
namespace {
constexpr std::uint64_t EncoderScratch=8*1024*1024;
Result<void> layout(const TileData& p,std::uint64_t max){
 if(p.width>16384||p.height>16384)return error(ErrorCode::BudgetExceeded,"ora.dimension-limit");
 if(!p.width||!p.height||p.format!=PixelFormat{ColorMode::RGB,Depth::U8,true})return error(ErrorCode::UnsupportedFeature,"ora.rgba8-required");
 auto bytes=checkedMultiply(p.rowStride,p.height);
 if(p.rowStride<std::uint64_t(p.width)*4 || !bytes || bytes.value()>p.bytes.size())return error(ErrorCode::CorruptData,"ora.raster-layout");
 if(bytes.value()>max || p.bytes.capacity()>max)return error(ErrorCode::BudgetExceeded,"ora.layer-limit");
 return {};
}
Result<std::string> xml(std::string_view s){
 if(s.size()>16384)return error(ErrorCode::BudgetExceeded,"ora.name-limit");
 for(std::size_t i=0;i<s.size();){
  const auto c=static_cast<unsigned char>(s[i++]);if(c<128)continue;
  unsigned count=0;std::uint32_t code=0,minimum=0;
  if(c>=0xc2&&c<=0xdf){count=1;code=c&31;minimum=0x80;}
  else if(c>=0xe0&&c<=0xef){count=2;code=c&15;minimum=0x800;}
  else if(c>=0xf0&&c<=0xf4){count=3;code=c&7;minimum=0x10000;}
  else return error(ErrorCode::InvalidInput,"ora.xml-utf8");
  if(count>s.size()-i)return error(ErrorCode::InvalidInput,"ora.xml-utf8");
  while(count--){const auto next=static_cast<unsigned char>(s[i++]);if((next&0xc0)!=0x80)return error(ErrorCode::InvalidInput,"ora.xml-utf8");code=(code<<6)|(next&63);}
  if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff)||code==0xfffe||code==0xffff)return error(ErrorCode::InvalidInput,"ora.xml-utf8");
 }
 std::string out;
 for(unsigned char c:s){
  if((c<32 && c!=9 && c!=10 && c!=13)||c==127)return error(ErrorCode::InvalidInput,"ora.xml-name");
  switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;case '\'':out+="&apos;";break;default:out+=char(c);}
 }
 return out;
}
TileData thumbnail(const TileData& source){
 const double scale=std::min({1.,256./source.width,256./source.height});
 TileData out;out.width=std::max(1u,unsigned(std::floor(source.width*scale)));out.height=std::max(1u,unsigned(std::floor(source.height*scale)));
 out.profileId=source.profileId;out.rowStride=std::uint64_t(out.width)*4;out.bytes.resize(out.rowStride*out.height);
 for(unsigned y=0;y<out.height;y++)for(unsigned x=0;x<out.width;x++){
  const auto sx=std::uint64_t(x)*source.width/out.width,sy=std::uint64_t(y)*source.height/out.height;
  std::copy_n(source.bytes.data()+sy*source.rowStride+sx*4,4,out.bytes.data()+y*out.rowStride+x*4);
 }return out;
}
}
Result<ExportReport> writeOraStream(const ExportPlan& plan,std::size_t layerCount,
 const OraLayerProvider& provider,const TileData& merged,const RasterEncodingOptions& options,
 OraStreamLimits limits,std::stop_token stop) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"ora.cancelled");
 if(plan.request.format!=ExportFormat::Ora || plan.request.path.empty() || !provider || !layerCount)
  return error(ErrorCode::InvalidInput,"ora.request");
 if(layerCount>limits.maxLayers || layerCount>65531 || !limits.maxOutputBytes || !limits.maxMetadataBytes)
  return error(ErrorCode::BudgetExceeded,"ora.entry-limit");
 if(merged.width!=plan.request.size.width || merged.height!=plan.request.size.height || plan.request.depth!=Depth::U8)
  return error(ErrorCode::InvalidInput,"ora.merged-size");
 auto valid=layout(merged,limits.maxLayerBytes);if(!valid)return valid.error();
 if(!std::isfinite(options.ppi)||options.ppi<=0||options.ppi>100000)return error(ErrorCode::InvalidInput,"ora.ppi");
 std::error_code ec;const auto targetStatus=std::filesystem::symlink_status(plan.request.path,ec);
 if(std::filesystem::is_symlink(targetStatus))return error(ErrorCode::PathEscape,"ora.destination-symlink");
 if(!plan.request.overwrite && std::filesystem::exists(targetStatus))return error(ErrorCode::InvalidInput,"export.replace-consent");
 const auto metadataInputs=options.iccProfile.size()+options.metadata.exif.size()+options.metadata.xmp.size();
 auto fixed=checkedAdd(merged.bytes.capacity(),EncoderScratch);if(!fixed)return fixed.error();
 auto metadataScratch=checkedMultiply(metadataInputs,3);if(!metadataScratch)return metadataScratch.error();
 fixed=checkedAdd(fixed.value(),metadataScratch.value());if(!fixed)return fixed.error();
 // Reserve metadata budget for stack.xml and ZIP index, and one thumbnail.
 fixed=checkedAdd(fixed.value(),limits.maxMetadataBytes);if(!fixed)return fixed.error();
 fixed=checkedAdd(fixed.value(),256*256*4);if(!fixed)return fixed.error();
 if(fixed.value()>=limits.maxWorkingBytes)return error(ErrorCode::BudgetExceeded,"ora.working-limit");
 auto png=[&](const TileData& pixels,bool alreadyCounted)->Result<Bytes>{
  auto ok=layout(pixels,limits.maxLayerBytes);if(!ok)return ok.error();
  const auto extra=alreadyCounted?0:pixels.bytes.capacity();
  if(extra>=limits.maxWorkingBytes-fixed.value())return error(ErrorCode::BudgetExceeded,"ora.working-limit");
  const auto cap=std::min(limits.maxPngBytes,limits.maxWorkingBytes-fixed.value()-extra);
  return encodePngBounded(pixels,options,cap,stop);
 };
 static std::atomic<std::uint64_t> serial{};
 auto stagedPath=plan.request.path;stagedPath+=".stage-ora-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial++);
 struct Cleanup {std::filesystem::path path;bool armed=false;~Cleanup(){if(armed){std::error_code ignored;std::filesystem::remove(path,ignored);}}} cleanup{stagedPath};
 NativeOutputFile output;auto created=output.createExclusive(stagedPath);if(!created)return created.error();cleanup.armed=true;
 Sha256State digest;
 ZipStreamLimits zipLimits;zipLimits.maxBytes=limits.maxOutputBytes;zipLimits.maxMetadataBytes=limits.maxMetadataBytes/2;zipLimits.maxEntries=std::uint32_t(layerCount+4);
 ZipStream zip([&](std::span<const std::uint8_t> bytes)->Result<void>{auto written=output.write(bytes);if(written)digest.update(bytes);return written;},zipLimits,stop);
 const std::string mime="image/openraster";
 auto emitted=zip.addKnown("mimetype",std::span(reinterpret_cast<const std::uint8_t*>(mime.data()),mime.size()));if(!emitted)return emitted.error();
 std::ostringstream stack;stack.imbue(std::locale::classic());
 stack<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?><image version=\"0.0.1\" w=\""<<merged.width<<"\" h=\""<<merged.height<<"\" name=\"Drawborad\" xres=\""<<options.ppi<<"\" yres=\""<<options.ppi<<"\"><stack>";
 for(std::size_t i=0;i<layerCount;i++){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"ora.cancelled");
  auto produced=provider(i,stop);if(!produced)return produced.error();
  auto& layer=produced.value();
  if(!std::isfinite(layer.opacity)||layer.opacity<0||layer.opacity>1||
     (layer.blend!=BlendMode::Normal&&layer.blend!=BlendMode::Multiply))return error(ErrorCode::UnsupportedFeature,"ora.layer-properties");
  if(layer.x<INT32_MIN||layer.x>INT32_MAX||layer.y<INT32_MIN||layer.y>INT32_MAX)return error(ErrorCode::InvalidInput,"ora.layer-offset");
  auto name=xml(layer.name);if(!name)return name.error();
  const auto path="data/layer"+std::to_string(i)+".png";
  stack<<"<layer name=\""<<name.value()<<"\" src=\""<<path<<"\" x=\""<<layer.x<<"\" y=\""<<layer.y<<"\" opacity=\""<<layer.opacity<<"\" visibility=\""<<(layer.visible?"visible":"hidden")<<"\" composite-op=\""<<(layer.blend==BlendMode::Multiply?"svg:multiply":"svg:src-over")<<"\"/>";
  if(std::uint64_t(stack.tellp())>limits.maxMetadataBytes/8)return error(ErrorCode::BudgetExceeded,"ora.xml-limit");
  auto encoded=png(layer.pixels,false);if(!encoded)return encoded.error();
  emitted=zip.addKnown(path,encoded.value());if(!emitted)return emitted.error();
  // produced and encoded are destroyed before requesting the next layer.
 }
 stack<<"</stack></image>";
 const auto text=stack.str();emitted=zip.addKnown("stack.xml",std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));if(!emitted)return emitted.error();
 {auto thumb=thumbnail(merged);auto encoded=png(thumb,true);if(!encoded)return encoded.error();emitted=zip.addKnown("Thumbnails/thumbnail.png",encoded.value());if(!emitted)return emitted.error();}
 {auto encoded=png(merged,true);if(!encoded)return encoded.error();emitted=zip.addKnown("mergedimage.png",encoded.value());if(!emitted)return emitted.error();}
 emitted=zip.finish();if(!emitted)return emitted.error();
 auto closed=output.close();if(!closed)return closed.error();
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"ora.cancelled");
 NativeFileIO io;const auto hash=digest.finish();
 auto committed=commitExport(io,{stagedPath,hash},{plan.request.path,plan.request.overwrite,true},limits.maxOutputBytes);if(!committed)return committed.error();
 ExportReport report;report.paths={plan.request.path};report.hashes={hash};report.dimensions=plan.request.size;report.channels=4;
 report.warnings={"ora-layer-locks-not-preserved"};
 if(plan.requiresRgbConversion)report.warnings.push_back("explicit-rgb-conversion");
 return report;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"ora.allocation");}
 catch(const std::filesystem::filesystem_error&){return error(ErrorCode::IoError,"ora.filesystem");}
 catch(...){return error(ErrorCode::IoError,"ora.provider-exception");}
}
