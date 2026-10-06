#include "RasterReaders.h"
#include "core/Digest.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <setjmp.h>

#ifdef DRAWBORAD_HAS_PNG
#include <png.h>
#endif
#ifdef DRAWBORAD_HAS_JPEG
extern "C" {
#include <jpeglib.h>
}
#endif
#ifdef DRAWBORAD_HAS_TIFF
#include <tiffio.h>
#endif
#ifdef DRAWBORAD_HAS_WEBP
#include <webp/decode.h>
#endif
#ifdef DRAWBORAD_HAS_WEBPMUX
#include <webp/mux.h>
#endif

namespace drawborad {
namespace {
std::uint16_t be16(const std::uint8_t* p){return std::uint16_t((p[0]<<8)|p[1]);}
std::uint32_t be32(const std::uint8_t* p){return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];}
std::uint16_t le16(const std::uint8_t* p){return std::uint16_t(p[0]|(std::uint16_t(p[1])<<8));}
std::uint32_t le32(const std::uint8_t* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}

Result<void> orient(TileData& tile,std::uint16_t orientation) {
    if(orientation<1||orientation>8)return error(ErrorCode::CorruptData,"codec.orientation");
    if(orientation==1)return {};
    const auto channels=tile.format.channelCount();
    const auto bytes=tile.format.depth==Depth::U16?2u:1u;
    const auto pixelBytes=channels*bytes;
    const auto sw=tile.width,sh=tile.height;
    const bool swap=orientation>=5;
    const auto dw=swap?sh:sw,dh=swap?sw:sh;
    auto row=checkedMultiply(dw,pixelBytes);if(!row)return row.error();
    auto total=checkedMultiply(row.value(),dh);if(!total||total.value()>SIZE_MAX)return error(ErrorCode::BudgetExceeded,"codec.orientation-size");
    Bytes out(std::size_t(total.value()));
    auto copy=[&](std::uint32_t dx,std::uint32_t dy,std::uint32_t sx,std::uint32_t sy){
        auto src=std::size_t(std::uint64_t(sy)*tile.rowStride+std::uint64_t(sx)*pixelBytes);
        auto dst=std::size_t(std::uint64_t(dy)*row.value()+std::uint64_t(dx)*pixelBytes);
        std::memcpy(out.data()+dst,tile.bytes.data()+src,pixelBytes);
    };
    for(std::uint32_t y=0;y<dh;y++)for(std::uint32_t x=0;x<dw;x++){
        std::uint32_t sx=0,sy=0;
        switch(orientation){
        case 2:sx=sw-1-x;sy=y;break;
        case 3:sx=sw-1-x;sy=sh-1-y;break;
        case 4:sx=x;sy=sh-1-y;break;
        case 5:sx=y;sy=x;break;
        case 6:sx=y;sy=sh-1-x;break;
        case 7:sx=sw-1-y;sy=sh-1-x;break;
        case 8:sx=sw-1-y;sy=x;break;
        default:sx=x;sy=y;break;
        }
        copy(x,y,sx,sy);
    }
    tile.width=dw;tile.height=dh;tile.rowStride=row.value();tile.bytes=std::move(out);
    return {};
}

std::uint16_t jpegOrientation(std::span<const std::uint8_t> b){
    std::size_t p=2;
    while(p+4<=b.size()&&b[p]==0xff){
        const auto marker=b[p+1];p+=2;
        if(marker==0xda||marker==0xd9)break;
        if(p+2>b.size())break;
        const auto len=be16(b.data()+p);if(len<2||p+len>b.size())break;
        if(marker==0xe1&&len>=10&&std::memcmp(b.data()+p+2,"Exif\0\0",6)==0){
            const auto* t=b.data()+p+8;const auto remain=len-8;
            if(remain<8)break;
            const bool little=t[0]=='I'&&t[1]=='I';const bool big=t[0]=='M'&&t[1]=='M';if(!little&&!big)break;
            auto u16=[&](const std::uint8_t*q){return little?le16(q):be16(q);};
            auto u32=[&](const std::uint8_t*q){return little?le32(q):be32(q);};
            const auto off=u32(t+4);if(std::uint64_t(off)>std::uint64_t(remain)-2)break;const auto count=u16(t+off);auto at=std::size_t(off)+2;
            if(std::uint64_t(count)*12>remain-at)break;
            for(std::uint16_t i=0;i<count;i++,at+=12){
                if(u16(t+at)==0x0112&&u16(t+at+2)==3&&u32(t+at+4)==1){
                    const auto o=u16(t+at+8);return o>=1&&o<=8?o:1;
                }
            }
        }
        p+=len;
    }
    return 1;
}

#ifdef DRAWBORAD_HAS_PNG
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4611)
#endif
struct PngRead { std::span<const std::uint8_t> data; std::size_t pos{}; };
void pngRead(png_structp png,png_bytep out,png_size_t n){
    auto* r=static_cast<PngRead*>(png_get_io_ptr(png));
    if(n>r->data.size()-r->pos)png_error(png,"truncated");
    std::memcpy(out,r->data.data()+r->pos,n);r->pos+=n;
}
Result<TileData> decodePng(std::span<const std::uint8_t>b,InputInfo& info){
    auto* png=png_create_read_struct(PNG_LIBPNG_VER_STRING,nullptr,nullptr,nullptr);if(!png)return error(ErrorCode::IoError,"png.read-create");
    auto* meta=png_create_info_struct(png);if(!meta){png_destroy_read_struct(&png,nullptr,nullptr);return error(ErrorCode::IoError,"png.read-info");}
    PngRead r{b,0};
    if(setjmp(png_jmpbuf(png))){png_destroy_read_struct(&png,&meta,nullptr);return error(ErrorCode::CorruptData,"png.decode");}
    png_set_read_fn(png,&r,pngRead);png_read_info(png,meta);
    auto w=png_get_image_width(png,meta),h=png_get_image_height(png,meta);auto depth=png_get_bit_depth(png,meta);auto type=png_get_color_type(png,meta);
    if(depth!=8&&depth!=16)png_set_expand_gray_1_2_4_to_8(png);
    if(type==PNG_COLOR_TYPE_PALETTE)png_set_palette_to_rgb(png);
    if(type==PNG_COLOR_TYPE_GRAY||type==PNG_COLOR_TYPE_GRAY_ALPHA)png_set_gray_to_rgb(png);
    if(png_get_valid(png,meta,PNG_INFO_tRNS))png_set_tRNS_to_alpha(png);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if(depth==16)png_set_swap(png);
#endif
    png_read_update_info(png,meta);
    depth=png_get_bit_depth(png,meta);auto channels=png_get_channels(png,meta);
    if((depth!=8&&depth!=16)||(channels!=3&&channels!=4)){png_destroy_read_struct(&png,&meta,nullptr);return error(ErrorCode::UnsupportedFeature,"png.layout");}
    TileData out;out.width=w;out.height=h;out.format={ColorMode::RGB,depth==16?Depth::U16:Depth::U8,channels==4};out.rowStride=png_get_rowbytes(png,meta);out.bytes.resize(std::size_t(out.rowStride)*h);
    std::vector<png_bytep> rows(h);for(std::uint32_t y=0;y<h;y++)rows[y]=out.bytes.data()+std::uint64_t(y)*out.rowStride;
    png_read_image(png,rows.data());
    png_charp name=nullptr;int comp=0;png_bytep profile=nullptr;png_uint_32 profileLen=0;
    if(png_get_iCCP(png,meta,&name,&comp,&profile,&profileLen)&&profile&&profileLen)info.icc=sha256(std::span<const std::uint8_t>(profile,profileLen));
    png_destroy_read_struct(&png,&meta,nullptr);out.profileId=info.icc;return out;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#else
Result<TileData> decodePng(std::span<const std::uint8_t>,InputInfo&){return error(ErrorCode::UnsupportedFeature,"png.codec-unavailable");}
#endif

#ifdef DRAWBORAD_HAS_JPEG
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4611 4324)
#endif
struct JpegErr { jpeg_error_mgr base; jmp_buf jump; };
void jpegError(j_common_ptr p){auto* e=reinterpret_cast<JpegErr*>(p->err);longjmp(e->jump,1);}
Result<TileData> decodeJpeg(std::span<const std::uint8_t>b,InputInfo& info){
    jpeg_decompress_struct d{};JpegErr e{};d.err=jpeg_std_error(&e.base);e.base.error_exit=jpegError;
    if(setjmp(e.jump)){jpeg_destroy_decompress(&d);return error(ErrorCode::CorruptData,"jpeg.decode");}
    if(b.size()>std::numeric_limits<unsigned long>::max())
        return error(ErrorCode::BudgetExceeded,"jpeg.input-size");
    jpeg_create_decompress(&d);
    jpeg_mem_src(&d,const_cast<unsigned char*>(b.data()),static_cast<unsigned long>(b.size()));
    jpeg_save_markers(&d,JPEG_APP0+2,0xffff);jpeg_read_header(&d,TRUE);d.out_color_space=JCS_RGB;jpeg_start_decompress(&d);
    TileData out;out.width=d.output_width;out.height=d.output_height;out.format={ColorMode::RGB,Depth::U8,false};out.rowStride=std::uint64_t(out.width)*3;out.bytes.resize(std::size_t(out.rowStride)*out.height);
    while(d.output_scanline<d.output_height){JSAMPROW row=out.bytes.data()+std::uint64_t(d.output_scanline)*out.rowStride;jpeg_read_scanlines(&d,&row,1);}
    std::vector<std::pair<int,Bytes>> parts;
    for(auto*m=d.marker_list;m;m=m->next)if(m->marker==JPEG_APP0+2&&m->data_length>=14&&std::memcmp(m->data,"ICC_PROFILE\0",12)==0){parts.push_back({m->data[12],Bytes(m->data+14,m->data+m->data_length)});}
    if(!parts.empty()){std::sort(parts.begin(),parts.end());Bytes icc;for(auto&part:parts)icc.insert(icc.end(),part.second.begin(),part.second.end());info.icc=sha256(icc);out.profileId=info.icc;}
    jpeg_finish_decompress(&d);jpeg_destroy_decompress(&d);return out;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#else
Result<TileData> decodeJpeg(std::span<const std::uint8_t>,InputInfo&){return error(ErrorCode::UnsupportedFeature,"jpeg.codec-unavailable");}
#endif

#ifdef DRAWBORAD_HAS_WEBP
Result<TileData> decodeWebp(std::span<const std::uint8_t>b,InputInfo& info){
#ifndef DRAWBORAD_HAS_WEBPMUX
    (void)info;
#endif
    WebPBitstreamFeatures f{};if(WebPGetFeatures(b.data(),b.size(),&f)!=VP8_STATUS_OK)return error(ErrorCode::CorruptData,"webp.decode");
    TileData out;out.width=f.width;out.height=f.height;out.format={ColorMode::RGB,Depth::U8,bool(f.has_alpha)};auto channels=f.has_alpha?4u:3u;out.rowStride=std::uint64_t(out.width)*channels;out.bytes.resize(std::size_t(out.rowStride)*out.height);
    auto* result=f.has_alpha?WebPDecodeRGBAInto(b.data(),b.size(),out.bytes.data(),out.bytes.size(),int(out.rowStride)):WebPDecodeRGBInto(b.data(),b.size(),out.bytes.data(),out.bytes.size(),int(out.rowStride));
    if(!result)return error(ErrorCode::CorruptData,"webp.decode");
#ifdef DRAWBORAD_HAS_WEBPMUX
    WebPData data{b.data(),b.size()};WebPMux* mux=WebPMuxCreate(&data,0);if(mux){WebPData icc{nullptr,0};if(WebPMuxGetChunk(mux,"ICCP",&icc)==WEBP_MUX_OK&&icc.bytes&&icc.size)info.icc=sha256(std::span<const std::uint8_t>(icc.bytes,icc.size));WebPDataClear(&icc);WebPMuxDelete(mux);}out.profileId=info.icc;
#endif
    return out;
}
#else
Result<TileData> decodeWebp(std::span<const std::uint8_t>,InputInfo&){return error(ErrorCode::UnsupportedFeature,"webp.codec-unavailable");}
#endif

Result<TileData> decodeBmp(std::span<const std::uint8_t>b,InputInfo& info){
    if(b.size()<54||b[0]!='B'||b[1]!='M')return error(ErrorCode::CorruptData,"bmp.header");
    auto offset=le32(b.data()+10),dib=le32(b.data()+14);if(dib<40||offset>b.size())return error(ErrorCode::UnsupportedFeature,"bmp.dib");
    const auto wi=std::int32_t(le32(b.data()+18));const auto hi=std::int32_t(le32(b.data()+22));const auto bits=le16(b.data()+28);const auto compression=le32(b.data()+30);
    if(wi<=0||hi==0||compression!=0||(bits!=24&&bits!=32))return error(ErrorCode::UnsupportedFeature,"bmp.layout");
    const auto w=std::uint32_t(wi);const auto h=std::uint32_t(hi<0?-std::int64_t(hi):hi);const auto srcRow=((std::uint64_t(w)*bits+31)/32)*4;
    auto need=checkedMultiply(srcRow,h);if(!need||offset+need.value()>b.size())return error(ErrorCode::CorruptData,"bmp.bounds");
    TileData out;out.width=w;out.height=h;out.format={ColorMode::RGB,Depth::U8,bits==32};auto channels=bits==32?4u:3u;out.rowStride=std::uint64_t(w)*channels;out.bytes.resize(std::size_t(out.rowStride)*h);
    for(std::uint32_t y=0;y<h;y++){auto sy=hi>0?h-1-y:y;auto*src=b.data()+offset+std::uint64_t(sy)*srcRow;auto*dst=out.bytes.data()+std::uint64_t(y)*out.rowStride;for(std::uint32_t x=0;x<w;x++){dst[x*channels]=src[x*(bits/8)+2];dst[x*channels+1]=src[x*(bits/8)+1];dst[x*channels+2]=src[x*(bits/8)];if(bits==32)dst[x*channels+3]=src[x*4+3];}}
    out.profileId=info.icc;return out;
}

#ifdef DRAWBORAD_HAS_TIFF
struct TiffReadMemory{std::span<const std::uint8_t>data;toff_t pos{};};
tmsize_t tr(thandle_t h,void*out,tmsize_t n){auto*m=static_cast<TiffReadMemory*>(h);if(n<0)return -1;auto remain=m->pos<m->data.size()?m->data.size()-std::size_t(m->pos):0;auto take=std::min<std::size_t>(remain,std::size_t(n));if(take)std::memcpy(out,m->data.data()+m->pos,take);m->pos+=take;return tmsize_t(take);}
tmsize_t tw(thandle_t,void*,tmsize_t){return -1;}
toff_t ts(thandle_t h,toff_t off,int whence){auto*m=static_cast<TiffReadMemory*>(h);toff_t p=whence==SEEK_SET?off:whence==SEEK_CUR?m->pos+off:whence==SEEK_END?m->data.size()+off:toff_t(-1);if(p==toff_t(-1))return p;m->pos=p;return p;}
int tc(thandle_t){return 0;}toff_t tz(thandle_t h){return static_cast<TiffReadMemory*>(h)->data.size();}int tm(thandle_t,void**,toff_t*){return 0;}void tu(thandle_t,void*,toff_t){}
Result<TileData> decodeTiff(std::span<const std::uint8_t>b,InputInfo& info){
    TiffReadMemory mem{b,0};TIFF*t=TIFFClientOpen("Drawborad","r",reinterpret_cast<thandle_t>(&mem),tr,tw,ts,tc,tz,tm,tu);if(!t)return error(ErrorCode::CorruptData,"tiff.open");
    std::uint32_t w=0,h=0;std::uint16_t bits=0,samples=0,photo=0,planar=0,orientation=1;TIFFGetField(t,TIFFTAG_IMAGEWIDTH,&w);TIFFGetField(t,TIFFTAG_IMAGELENGTH,&h);TIFFGetFieldDefaulted(t,TIFFTAG_BITSPERSAMPLE,&bits);TIFFGetFieldDefaulted(t,TIFFTAG_SAMPLESPERPIXEL,&samples);TIFFGetField(t,TIFFTAG_PHOTOMETRIC,&photo);TIFFGetFieldDefaulted(t,TIFFTAG_PLANARCONFIG,&planar);TIFFGetFieldDefaulted(t,TIFFTAG_ORIENTATION,&orientation);
    const bool cmyk=photo==PHOTOMETRIC_SEPARATED;const unsigned colors=cmyk?4u:3u;if(!w||!h||(bits!=8&&bits!=16)||planar!=PLANARCONFIG_CONTIG||(photo!=PHOTOMETRIC_RGB&&!cmyk)||(samples!=colors&&samples!=colors+1)){TIFFClose(t);return error(ErrorCode::UnsupportedFeature,"tiff.layout");}
    if(cmyk){std::uint16_t ink=0;TIFFGetFieldDefaulted(t,TIFFTAG_INKSET,&ink);if(ink!=INKSET_CMYK){TIFFClose(t);return error(ErrorCode::UnsupportedFeature,"tiff.inkset");}}
    TileData out;out.width=w;out.height=h;out.format={cmyk?ColorMode::CMYK:ColorMode::RGB,bits==16?Depth::U16:Depth::U8,samples==colors+1};out.rowStride=TIFFScanlineSize64(t);auto total=checkedMultiply(out.rowStride,h);if(!total||total.value()>SIZE_MAX){TIFFClose(t);return error(ErrorCode::BudgetExceeded,"tiff.size");}out.bytes.resize(std::size_t(total.value()));
    for(std::uint32_t y=0;y<h;y++)if(TIFFReadScanline(t,out.bytes.data()+std::uint64_t(y)*out.rowStride,y,0)<0){TIFFClose(t);return error(ErrorCode::CorruptData,"tiff.scanline");}
    std::uint32_t iccSize=0;void*icc=nullptr;if(TIFFGetField(t,TIFFTAG_ICCPROFILE,&iccSize,&icc)&&icc&&iccSize)info.icc=sha256(std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(icc),iccSize));
    info.orientation=orientation;out.profileId=info.icc;TIFFClose(t);return out;
}
#else
Result<TileData> decodeTiff(std::span<const std::uint8_t>,InputInfo&){return error(ErrorCode::UnsupportedFeature,"tiff.codec-unavailable");}
#endif
}

Result<TileData> decodeRasterBytes(std::span<const std::uint8_t> bytes,InputInfo& info,const DecodeOptions& options){
    Result<TileData> decoded=error(ErrorCode::UnsupportedFeature,"codec.not-raster");
    switch(info.format){
    case ImageFormat::Png:decoded=decodePng(bytes,info);break;
    case ImageFormat::Jpeg:info.orientation=jpegOrientation(bytes);decoded=decodeJpeg(bytes,info);break;
    case ImageFormat::Webp:decoded=decodeWebp(bytes,info);break;
    case ImageFormat::Tiff:decoded=decodeTiff(bytes,info);break;
    case ImageFormat::Bmp:decoded=decodeBmp(bytes,info);break;
    default:return error(ErrorCode::UnsupportedFeature,"codec.layered-or-optional");
    }
    if(!decoded)return decoded.error();
    if(info.icc.empty()&&!options.explicitProfile.empty()){info.icc=options.explicitProfile;decoded.value().profileId=options.explicitProfile;}
    if(options.applyOrientation&&info.orientation!=1){auto ok=orient(decoded.value(),info.orientation);if(!ok)return ok.error();info.orientedSize={decoded.value().width,decoded.value().height};}else info.orientedSize={decoded.value().width,decoded.value().height};
    auto bytesResult=checkedMultiply(decoded.value().rowStride,decoded.value().height);if(!bytesResult||bytesResult.value()>options.limits.decodedBytes)return error(ErrorCode::BudgetExceeded,"codec.decode-budget");
    return decoded;
}
}
