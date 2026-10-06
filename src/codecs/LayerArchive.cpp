#include "LayerArchive.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace drawborad {
namespace {
void le16(Bytes& b,std::uint16_t v){b.push_back(std::uint8_t(v));b.push_back(std::uint8_t(v>>8));}
void le32(Bytes& b,std::uint32_t v){b.push_back(std::uint8_t(v));b.push_back(std::uint8_t(v>>8));b.push_back(std::uint8_t(v>>16));b.push_back(std::uint8_t(v>>24));}
std::uint32_t crc32(std::span<const std::uint8_t> data){
    std::uint32_t crc=0xffffffffu;
    for(auto byte:data){crc^=byte;for(int k=0;k<8;k++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}
    return ~crc;
}
struct ZipEntry{std::string name;Bytes data;std::uint32_t offset{},crc{};};
Result<Bytes> zipStore(std::vector<ZipEntry> entries){
    if(entries.empty()||entries.size()>65535)return error(ErrorCode::BudgetExceeded,"zip.entry-count");
    Bytes out;
    for(auto& e:entries){
        if(e.name.empty()||e.name.size()>65535||e.data.size()>UINT32_MAX||out.size()>UINT32_MAX)
            return error(ErrorCode::BudgetExceeded,"zip.classic-limit");
        if(e.name.find("..")!=std::string::npos||e.name.front()=='/'||e.name.front()=='\\')
            return error(ErrorCode::PathEscape,"zip.entry-path");
        e.offset=std::uint32_t(out.size());e.crc=crc32(e.data);
        le32(out,0x04034b50);le16(out,20);le16(out,0x0800);le16(out,0);le16(out,0);le16(out,0);
        le32(out,e.crc);le32(out,std::uint32_t(e.data.size()));le32(out,std::uint32_t(e.data.size()));
        le16(out,std::uint16_t(e.name.size()));le16(out,0);
        out.insert(out.end(),e.name.begin(),e.name.end());out.insert(out.end(),e.data.begin(),e.data.end());
    }
    if(out.size()>UINT32_MAX)return error(ErrorCode::BudgetExceeded,"zip.classic-limit");
    const auto centralOffset=std::uint32_t(out.size());
    for(const auto& e:entries){
        le32(out,0x02014b50);le16(out,20);le16(out,20);le16(out,0x0800);le16(out,0);le16(out,0);le16(out,0);
        le32(out,e.crc);le32(out,std::uint32_t(e.data.size()));le32(out,std::uint32_t(e.data.size()));
        le16(out,std::uint16_t(e.name.size()));le16(out,0);le16(out,0);le16(out,0);le16(out,0);le32(out,0);le32(out,e.offset);
        out.insert(out.end(),e.name.begin(),e.name.end());
    }
    if(out.size()>UINT32_MAX)return error(ErrorCode::BudgetExceeded,"zip.classic-limit");
    const auto centralSize=std::uint32_t(out.size()-centralOffset);
    le32(out,0x06054b50);le16(out,0);le16(out,0);le16(out,std::uint16_t(entries.size()));le16(out,std::uint16_t(entries.size()));
    le32(out,centralSize);le32(out,centralOffset);le16(out,0);
    return out;
}
std::string xml(std::string_view s){
    std::string o;
    for(char c:s){switch(c){case '&':o+="&amp;";break;case '<':o+="&lt;";break;case '>':o+="&gt;";break;case '"':o+="&quot;";break;case '\'':o+="&apos;";break;default:o+=c;}}
    return o;
}
std::string json(std::string_view s){
    static const char hex[]="0123456789abcdef";
    std::string o;
    for(unsigned char c:s){
        switch(c){
        case '"': o+="\\\""; break;
        case '\\': o+="\\\\"; break;
        case '\b': o+="\\b"; break;
        case '\f': o+="\\f"; break;
        case '\n': o+="\\n"; break;
        case '\r': o+="\\r"; break;
        case '\t': o+="\\t"; break;
        default:
            if(c<0x20){o+="\\u00";o+=hex[c>>4];o+=hex[c&15];}
            else o+=char(c);
        }
    }
    return o;
}
const char* composite(BlendMode b){
    switch(b){case BlendMode::Normal:return "svg:src-over";case BlendMode::Multiply:return "svg:multiply";case BlendMode::Screen:return "svg:screen";case BlendMode::Darken:return "svg:darken";case BlendMode::Lighten:return "svg:lighten";}
    return "svg:src-over";
}
Result<Bytes> pngLayer(const ArchiveLayer& layer,const RasterEncodingOptions& o){
    if(layer.pixels.format.mode!=ColorMode::RGB)return error(ErrorCode::ConversionConsentRequired,"archive.rgb-required");
    return encodeRaster(ExportFormat::Png,layer.pixels,o);
}
Result<TileData> thumbnailPixels(const TileData& source){
    if(source.format.mode!=ColorMode::RGB)
        return error(ErrorCode::ConversionConsentRequired,"ora.thumbnail-rgb-required");
    if(!source.width||!source.height)
        return error(ErrorCode::InvalidInput,"ora.thumbnail-empty");
    if(source.format.depth!=Depth::U8&&source.format.depth!=Depth::U16)
        return error(ErrorCode::UnsupportedFeature,"ora.thumbnail-depth");
    const auto channels=source.format.alpha?4u:3u;
    const auto sampleBytes=source.format.depth==Depth::U16?2u:1u;
    const auto sourceRow=std::uint64_t(source.width)*channels*sampleBytes;
    if(source.rowStride<sourceRow)
        return error(ErrorCode::CorruptData,"ora.thumbnail-stride");
    auto sourceBytes=checkedMultiply(source.rowStride,source.height);
    if(!sourceBytes||sourceBytes.value()>source.bytes.size())
        return error(ErrorCode::CorruptData,"ora.thumbnail-bytes");

    double scale=std::min({1.0,256.0/double(source.width),256.0/double(source.height)});
    auto targetWidth=std::max<std::uint32_t>(1,std::uint32_t(std::floor(double(source.width)*scale)));
    auto targetHeight=std::max<std::uint32_t>(1,std::uint32_t(std::floor(double(source.height)*scale)));
    targetWidth=std::min<std::uint32_t>(targetWidth,256);
    targetHeight=std::min<std::uint32_t>(targetHeight,256);

    TileData out;
    out.width=targetWidth;
    out.height=targetHeight;
    out.format={ColorMode::RGB,Depth::U8,source.format.alpha};
    out.profileId=source.profileId;
    out.rowStride=std::uint64_t(targetWidth)*channels;
    auto total=checkedMultiply(out.rowStride,out.height);
    if(!total||total.value()>SIZE_MAX)
        return error(ErrorCode::BudgetExceeded,"ora.thumbnail-size");
    out.bytes.resize(std::size_t(total.value()));

    for(std::uint32_t y=0;y<targetHeight;++y){
        const auto sy=std::min<std::uint32_t>(source.height-1,
            std::uint32_t((std::uint64_t(y)*source.height)/targetHeight));
        for(std::uint32_t x=0;x<targetWidth;++x){
            const auto sx=std::min<std::uint32_t>(source.width-1,
                std::uint32_t((std::uint64_t(x)*source.width)/targetWidth));
            const auto srcAt=std::uint64_t(sy)*source.rowStride+
                             std::uint64_t(sx)*channels*sampleBytes;
            const auto dstAt=std::uint64_t(y)*out.rowStride+std::uint64_t(x)*channels;
            for(unsigned c=0;c<channels;++c){
                std::uint16_t value=0;
                if(sampleBytes==1){
                    value=std::uint16_t(source.bytes[std::size_t(srcAt+c)])*257u;
                }else{
                    const auto at=std::size_t(srcAt+std::uint64_t(c)*2u);
                    value=std::uint16_t(source.bytes[at] |
                        (std::uint16_t(source.bytes[at+1])<<8));
                }
                out.bytes[std::size_t(dstAt+c)]=std::uint8_t((std::uint32_t(value)+128u)/257u);
            }
        }
    }
    return out;
}
}

Result<Bytes> encodeOra(PixelSize size,std::span<const ArchiveLayer> layers,const TileData& merged,const RasterEncodingOptions& options){
    if(!size.width||!size.height||layers.empty())return error(ErrorCode::InvalidInput,"ora.document");
    if(merged.width!=size.width||merged.height!=size.height||merged.format.mode!=ColorMode::RGB)
        return error(ErrorCode::InvalidInput,"ora.merged");
    auto mergedPng=encodeRaster(ExportFormat::Png,merged,options);if(!mergedPng)return mergedPng.error();
    auto thumbnail=thumbnailPixels(merged);if(!thumbnail)return thumbnail.error();
    auto thumbnailPng=encodeRaster(ExportFormat::Png,thumbnail.value(),options);if(!thumbnailPng)return thumbnailPng.error();
    std::vector<ZipEntry> entries;
    entries.push_back({"mimetype",Bytes{'i','m','a','g','e','/','o','p','e','n','r','a','s','t','e','r'}});
    std::ostringstream stack;
    stack<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<image version=\"0.0.1\" w=\""<<size.width<<"\" h=\""<<size.height<<"\" name=\"Drawborad\"><stack>";
    for(std::size_t i=0;i<layers.size();++i){
        const auto& layer=layers[i];auto png=pngLayer(layer,options);if(!png)return png.error();
        const auto path="data/layer"+std::to_string(i)+".png";
        stack<<"<layer name=\""<<xml(layer.name)<<"\" src=\""<<path<<"\" x=\""<<layer.x<<"\" y=\""<<layer.y
             <<"\" opacity=\""<<std::clamp(layer.opacity,0.0,1.0)<<"\" visibility=\""<<(layer.visible?"visible":"hidden")
             <<"\" composite-op=\""<<composite(layer.blend)<<"\"/>";
        entries.push_back({path,std::move(png.value())});
    }
    stack<<"</stack></image>";
    auto stackText=stack.str();entries.insert(entries.begin()+1,{"stack.xml",Bytes(stackText.begin(),stackText.end())});
    entries.push_back({"Thumbnails/thumbnail.png",std::move(thumbnailPng.value())});
    entries.push_back({"mergedimage.png",std::move(mergedPng.value())});
    return zipStore(std::move(entries));
}

Result<Bytes> encodeLayerZip(PixelSize size,std::span<const ArchiveLayer> layers,const RasterEncodingOptions& options){
    if(!size.width||!size.height||layers.empty())return error(ErrorCode::InvalidInput,"layerzip.document");
    std::vector<ZipEntry> entries;
    std::ostringstream manifest;
    manifest<<"{\"schema\":1,\"width\":"<<size.width<<",\"height\":"<<size.height<<",\"layers\":[";
    for(std::size_t i=0;i<layers.size();++i){
        const auto& layer=layers[i];auto png=pngLayer(layer,options);if(!png)return png.error();
        auto path="layers/"+std::to_string(i)+".png";
        if(i)manifest<<",";
        manifest<<"{\"name\":\""<<json(layer.name)<<"\",\"file\":\""<<path<<"\",\"x\":"<<layer.x<<",\"y\":"<<layer.y
                <<",\"opacity\":"<<std::clamp(layer.opacity,0.0,1.0)<<",\"visible\":"<<(layer.visible?"true":"false")<<"}";
        entries.push_back({path,std::move(png.value())});
    }
    manifest<<"]}";
    auto text=manifest.str();entries.insert(entries.begin(),{"manifest.json",Bytes(text.begin(),text.end())});
    return zipStore(std::move(entries));
}
}
