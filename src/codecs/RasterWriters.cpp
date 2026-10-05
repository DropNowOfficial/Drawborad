#include "RasterWriters.h"
#include "storage/FileIO.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
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
#include <webp/encode.h>
#endif
#ifdef DRAWBORAD_HAS_WEBPMUX
#include <webp/mux.h>
#endif

namespace drawborad {
namespace {
unsigned colorChannels(ColorMode mode) { return mode==ColorMode::RGB ? 3u : 4u; }
unsigned sampleBytes(Depth depth) { return depth==Depth::U16 ? 2u : 1u; }

[[maybe_unused]] Result<void> validateRaster(const TileData& p) {
    if(!p.width||!p.height)
        return error(ErrorCode::InvalidInput,"export.empty-raster");
    const auto colors=colorChannels(p.format.mode);
    const auto channels=colors+(p.format.alpha?1u:0u);
    const auto bytes=sampleBytes(p.format.depth);
    auto row=checkedMultiply(p.width,std::uint64_t(channels*bytes));
    if(!row)return row.error();
    if(p.rowStride<row.value())
        return error(ErrorCode::CorruptData,"export.raster-stride");
    auto total=checkedMultiply(p.rowStride,p.height);
    if(!total||total.value()>p.bytes.size())
        return error(ErrorCode::CorruptData,"export.raster-bytes");
    if(p.format.depth!=Depth::U8&&p.format.depth!=Depth::U16)
        return error(ErrorCode::UnsupportedFeature,"export.raster-depth");
    return {};
}

#ifdef DRAWBORAD_HAS_PNG
struct PngWriteState { Bytes bytes; };
void pngWrite(png_structp png,png_bytep data,png_size_t length) {
    auto* state=static_cast<PngWriteState*>(png_get_io_ptr(png));
    state->bytes.insert(state->bytes.end(),data,data+length);
}
void pngFlush(png_structp) {}

Result<Bytes> encodePng(const TileData& p,const RasterEncodingOptions& o) {
    if(p.format.mode!=ColorMode::RGB)
        return error(ErrorCode::UnsupportedFeature,"png.rgb-only");
    auto valid=validateRaster(p);if(!valid)return valid.error();
    auto* png=png_create_write_struct(PNG_LIBPNG_VER_STRING,nullptr,nullptr,nullptr);
    if(!png)return error(ErrorCode::IoError,"png.create");
    auto* info=png_create_info_struct(png);
    if(!info){png_destroy_write_struct(&png,nullptr);return error(ErrorCode::IoError,"png.info");}
    PngWriteState state;
    if(setjmp(png_jmpbuf(png))){png_destroy_write_struct(&png,&info);return error(ErrorCode::CorruptData,"png.encode");}
    png_set_write_fn(png,&state,pngWrite,pngFlush);
    const int colorType=p.format.alpha?PNG_COLOR_TYPE_RGBA:PNG_COLOR_TYPE_RGB;
    png_set_IHDR(png,info,p.width,p.height,p.format.depth==Depth::U16?16:8,colorType,
                 PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
    if(std::isfinite(o.ppi)&&o.ppi>0){
        const auto ppm=png_uint_32(std::llround(o.ppi/0.0254));
        png_set_pHYs(png,info,ppm,ppm,PNG_RESOLUTION_METER);
    }
    if(!o.iccProfile.empty())
        png_set_iCCP(png,info,"Drawborad ICC",0,
                     reinterpret_cast<png_const_bytep>(o.iccProfile.data()),
                     png_uint_32(o.iccProfile.size()));
    png_write_info(png,info);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if(p.format.depth==Depth::U16) png_set_swap(png);
#endif
    std::vector<png_bytep> rows(p.height);
    for(std::uint32_t y=0;y<p.height;y++)
        rows[y]=const_cast<png_bytep>(reinterpret_cast<png_const_bytep>(p.bytes.data()+std::uint64_t(y)*p.rowStride));
    png_write_image(png,rows.data());
    png_write_end(png,info);
    png_destroy_write_struct(&png,&info);
    return state.bytes;
}
#else
Result<Bytes> encodePng(const TileData&,const RasterEncodingOptions&) {
    return error(ErrorCode::UnsupportedFeature,"png.codec-unavailable");
}
#endif

#ifdef DRAWBORAD_HAS_JPEG
struct JpegError {
    jpeg_error_mgr base;
    jmp_buf jump;
};
void jpegErrorExit(j_common_ptr cinfo) {
    auto* e=reinterpret_cast<JpegError*>(cinfo->err);
    longjmp(e->jump,1);
}
void writeIccMarkers(j_compress_ptr cinfo,std::span<const std::uint8_t> profile) {
    constexpr std::size_t header=14;
    constexpr std::size_t maxChunk=65533-header;
    const auto count=std::uint8_t((profile.size()+maxChunk-1)/maxChunk);
    std::size_t offset=0;
    for(std::uint8_t seq=1;seq<=count;seq++){
        const auto n=std::min(maxChunk,profile.size()-offset);
        std::vector<JOCTET> marker(header+n);
        const char sig[]="ICC_PROFILE";
        std::memcpy(marker.data(),sig,11);
        marker[11]=0;
        marker[12]=seq;
        marker[13]=count;
        std::memcpy(marker.data()+header,profile.data()+offset,n);
        jpeg_write_marker(cinfo,JPEG_APP0+2,marker.data(),unsigned(marker.size()));
        offset+=n;
    }
}
Result<Bytes> encodeJpeg(const TileData& p,const RasterEncodingOptions& o) {
    if(p.format.mode!=ColorMode::RGB||p.format.depth!=Depth::U8)
        return error(ErrorCode::UnsupportedFeature,"jpeg.rgb8-only");
    auto valid=validateRaster(p);if(!valid)return valid.error();
    jpeg_compress_struct cinfo{};
    JpegError err{};
    cinfo.err=jpeg_std_error(&err.base);
    err.base.error_exit=jpegErrorExit;
    if(setjmp(err.jump)){jpeg_destroy_compress(&cinfo);return error(ErrorCode::CorruptData,"jpeg.encode");}
    jpeg_create_compress(&cinfo);
    unsigned char* mem=nullptr; unsigned long memSize=0;
    jpeg_mem_dest(&cinfo,&mem,&memSize);
    cinfo.image_width=p.width;cinfo.image_height=p.height;cinfo.input_components=3;cinfo.in_color_space=JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo,std::clamp(o.quality,1,100),TRUE);
    if(std::isfinite(o.ppi)&&o.ppi>0&&o.ppi<=65535){cinfo.density_unit=1;cinfo.X_density=cinfo.Y_density=UINT16(std::llround(o.ppi));}
    jpeg_start_compress(&cinfo,TRUE);
    if(!o.iccProfile.empty()) writeIccMarkers(&cinfo,o.iccProfile);
    std::vector<std::uint8_t> row(std::size_t(p.width)*3);
    const unsigned channels=p.format.alpha?4u:3u;
    while(cinfo.next_scanline<cinfo.image_height){
        const auto y=cinfo.next_scanline;
        const auto* src=p.bytes.data()+std::uint64_t(y)*p.rowStride;
        for(std::uint32_t x=0;x<p.width;x++){
            if(p.format.alpha){
                const auto a=src[x*channels+3];
                for(unsigned k=0;k<3;k++)
                    row[std::size_t(x)*3+k]=std::uint8_t((std::uint32_t(src[x*channels+k])*a+
                        std::uint32_t(o.matte[k])*(255-a)+127)/255);
            } else {
                row[std::size_t(x)*3]=src[x*3];
                row[std::size_t(x)*3+1]=src[x*3+1];
                row[std::size_t(x)*3+2]=src[x*3+2];
            }
        }
        JSAMPROW rp=row.data();jpeg_write_scanlines(&cinfo,&rp,1);
    }
    jpeg_finish_compress(&cinfo);
    Bytes out(mem,mem+memSize);
    std::free(mem);
    jpeg_destroy_compress(&cinfo);
    return out;
}
#else
Result<Bytes> encodeJpeg(const TileData&,const RasterEncodingOptions&) {
    return error(ErrorCode::UnsupportedFeature,"jpeg.codec-unavailable");
}
#endif

#ifdef DRAWBORAD_HAS_TIFF
struct TiffMemory { Bytes data; toff_t pos{}; };
tmsize_t tiffRead(thandle_t h,void* data,tmsize_t n){
    auto* m=static_cast<TiffMemory*>(h);if(n<0)return -1;
    auto remain=m->pos<m->data.size()?m->data.size()-std::size_t(m->pos):0;
    auto take=std::min<std::size_t>(remain,std::size_t(n));
    if(take)
        std::memcpy(data,m->data.data()+m->pos,take);
    m->pos+=take;
    return tmsize_t(take);
}
tmsize_t tiffWrite(thandle_t h,void* data,tmsize_t n){
    auto* m=static_cast<TiffMemory*>(h);
    if(n<0)return -1;
    const auto end=m->pos+toff_t(n);
    if(end>std::numeric_limits<std::size_t>::max())return -1;
    if(std::size_t(end)>m->data.size())
        m->data.resize(std::size_t(end));
    std::memcpy(m->data.data()+m->pos,data,std::size_t(n));
    m->pos=end;
    return n;
}
toff_t tiffSeek(thandle_t h,toff_t off,int whence){
    auto* m=static_cast<TiffMemory*>(h);
    toff_t next=0;
    if(whence==SEEK_SET) next=off;
    else if(whence==SEEK_CUR) next=m->pos+off;
    else if(whence==SEEK_END) next=m->data.size()+off;
    else return toff_t(-1);
    m->pos=next;
    return next;
}
int tiffClose(thandle_t){return 0;}
toff_t tiffSize(thandle_t h){return static_cast<TiffMemory*>(h)->data.size();}
int tiffMap(thandle_t,void**,toff_t*){return 0;}
void tiffUnmap(thandle_t,void*,toff_t){}

Result<Bytes> encodeTiff(const TileData& p,const RasterEncodingOptions& o){
    auto valid=validateRaster(p);if(!valid)return valid.error();
    TiffMemory mem;
    TIFF* t=TIFFClientOpen("Drawborad","w",reinterpret_cast<thandle_t>(&mem),
        tiffRead,tiffWrite,tiffSeek,tiffClose,tiffSize,tiffMap,tiffUnmap);
    if(!t)return error(ErrorCode::IoError,"tiff.create");
    const auto colors=colorChannels(p.format.mode);const auto channels=colors+(p.format.alpha?1u:0u);
    TIFFSetField(t,TIFFTAG_IMAGEWIDTH,p.width);TIFFSetField(t,TIFFTAG_IMAGELENGTH,p.height);
    TIFFSetField(t,TIFFTAG_BITSPERSAMPLE,p.format.depth==Depth::U16?16:8);
    TIFFSetField(t,TIFFTAG_SAMPLESPERPIXEL,channels);TIFFSetField(t,TIFFTAG_PLANARCONFIG,PLANARCONFIG_CONTIG);
    TIFFSetField(t,TIFFTAG_PHOTOMETRIC,p.format.mode==ColorMode::CMYK?PHOTOMETRIC_SEPARATED:PHOTOMETRIC_RGB);
    if(p.format.mode==ColorMode::CMYK)TIFFSetField(t,TIFFTAG_INKSET,INKSET_CMYK);
    TIFFSetField(t,TIFFTAG_COMPRESSION,COMPRESSION_LZW);
    if(p.format.alpha){std::uint16_t extra=EXTRASAMPLE_UNASSALPHA;TIFFSetField(t,TIFFTAG_EXTRASAMPLES,1,&extra);}
    if(std::isfinite(o.ppi)&&o.ppi>0){float res=float(o.ppi);TIFFSetField(t,TIFFTAG_XRESOLUTION,res);TIFFSetField(t,TIFFTAG_YRESOLUTION,res);TIFFSetField(t,TIFFTAG_RESOLUTIONUNIT,RESUNIT_INCH);}
    if(!o.iccProfile.empty())TIFFSetField(t,TIFFTAG_ICCPROFILE,std::uint32_t(o.iccProfile.size()),const_cast<std::uint8_t*>(o.iccProfile.data()));
    bool ok=true;
    for(std::uint32_t y=0;y<p.height;y++)if(TIFFWriteScanline(t,const_cast<std::uint8_t*>(p.bytes.data()+std::uint64_t(y)*p.rowStride),y,0)<0){ok=false;break;}
    TIFFClose(t);if(!ok)return error(ErrorCode::IoError,"tiff.write");return mem.data;
}
#else
Result<Bytes> encodeTiff(const TileData&,const RasterEncodingOptions&) {
    return error(ErrorCode::UnsupportedFeature,"tiff.codec-unavailable");
}
#endif

#ifdef DRAWBORAD_HAS_WEBP
Result<Bytes> encodeWebp(const TileData& p,const RasterEncodingOptions& o){
    if(p.format.mode!=ColorMode::RGB||p.format.depth!=Depth::U8)
        return error(ErrorCode::UnsupportedFeature,"webp.rgb8-only");
    auto valid=validateRaster(p);if(!valid)return valid.error();
    std::uint8_t* encoded=nullptr;std::size_t n=0;
    if(p.format.alpha)n=WebPEncodeLosslessRGBA(p.bytes.data(),p.width,p.height,int(p.rowStride),&encoded);
    else n=WebPEncodeLosslessRGB(p.bytes.data(),p.width,p.height,int(p.rowStride),&encoded);
    if(!n||!encoded)return error(ErrorCode::IoError,"webp.encode");
    Bytes out(encoded,encoded+n);WebPFree(encoded);
#ifdef DRAWBORAD_HAS_WEBPMUX
    if(!o.iccProfile.empty()){
        WebPData bitstream{out.data(),out.size()},icc{o.iccProfile.data(),o.iccProfile.size()},assembled{nullptr,0};
        WebPMux* mux=WebPMuxCreate(&bitstream,1);if(!mux)return error(ErrorCode::IoError,"webp.mux");
        auto set=WebPMuxSetChunk(mux,"ICCP",&icc,1);auto get=set==WEBP_MUX_OK?WebPMuxAssemble(mux,&assembled):set;WebPMuxDelete(mux);
        if(get!=WEBP_MUX_OK)return error(ErrorCode::IoError,"webp.icc");
        out.assign(assembled.bytes,assembled.bytes+assembled.size);WebPDataClear(&assembled);
    }
#else
    if(!o.iccProfile.empty())return error(ErrorCode::UnsupportedFeature,"webp.icc-mux-unavailable");
#endif
    return out;
}
#else
Result<Bytes> encodeWebp(const TileData&,const RasterEncodingOptions&) {
    return error(ErrorCode::UnsupportedFeature,"webp.codec-unavailable");
}
#endif
}

Result<Bytes> encodeRaster(ExportFormat format,const TileData& pixels,const RasterEncodingOptions& options){
    switch(format){
    case ExportFormat::Png:return encodePng(pixels,options);
    case ExportFormat::Tiff:return encodeTiff(pixels,options);
    case ExportFormat::Jpeg:return encodeJpeg(pixels,options);
    case ExportFormat::Webp:return encodeWebp(pixels,options);
    default:return error(ErrorCode::UnsupportedFeature,"export.not-raster-format");
    }
}

Result<ExportReport> writeRaster(const ExportPlan& plan,const TileData& pixels,
                                 const RasterEncodingOptions& options,std::stop_token stop){
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"export.cancelled");
    if(plan.request.size.width!=pixels.width||plan.request.size.height!=pixels.height)
        return error(ErrorCode::InvalidInput,"export.dimension-mismatch");
    if(plan.requiresRgbConversion&&pixels.format.mode!=ColorMode::RGB)
        return error(ErrorCode::ConversionConsentRequired,"export.rgb-conversion-not-applied");
    auto encoded=encodeRaster(plan.request.format,pixels,options);if(!encoded)return encoded.error();
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"export.cancelled");
    NativeFileIO io;auto written=atomicWrite(io,plan.request.path,encoded.value());if(!written)return written.error();
    ExportReport report;report.paths={plan.request.path};report.hashes={sha256(encoded.value())};report.dimensions=plan.request.size;
    report.channels=colorChannels(pixels.format.mode)+(pixels.format.alpha?1u:0u);
    if(plan.requiresRgbConversion)report.warnings.push_back("explicit-rgb-conversion");
    return report;
}
}
