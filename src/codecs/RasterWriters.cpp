#include "RasterWriters.h"
#include "storage/FileIO.h"
#include "codecs/AtomicExport.h"
#include <atomic>
#include <chrono>
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

Result<void> validateMetadata(const RasterEncodingOptions& o) {
    if(o.metadata.preserveExif && o.metadata.exifContainsGps && !o.metadata.allowGps)
        return error(ErrorCode::ConversionConsentRequired,"export.gps-consent");
    if(o.metadata.exif.size()>16*1024*1024 || o.metadata.xmp.size()>16*1024*1024)
        return error(ErrorCode::BudgetExceeded,"export.metadata-size");
    return {};
}

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
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4611)
#endif
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
    if(o.metadata.preserveExif && !o.metadata.exif.empty()) {
#if defined(PNG_eXIf_SUPPORTED)
        png_set_eXIf_1(png,info,png_uint_32(o.metadata.exif.size()),
                       const_cast<png_bytep>(o.metadata.exif.data()));
#else
        png_destroy_write_struct(&png,&info);
        return error(ErrorCode::UnsupportedFeature,"png.exif-unavailable");
#endif
    }
    png_text xmpText{};
    if(o.metadata.preserveXmp && !o.metadata.xmp.empty()) {
        xmpText.compression=PNG_ITXT_COMPRESSION_NONE;
        xmpText.key=const_cast<png_charp>("XML:com.adobe.xmp");
        xmpText.text=reinterpret_cast<png_charp>(const_cast<std::uint8_t*>(o.metadata.xmp.data()));
        xmpText.text_length=o.metadata.xmp.size();
        xmpText.itxt_length=o.metadata.xmp.size();
        png_set_text(png,info,&xmpText,1);
    }
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
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#else
Result<Bytes> encodePng(const TileData&,const RasterEncodingOptions&) {
    return error(ErrorCode::UnsupportedFeature,"png.codec-unavailable");
}
#endif

#ifdef DRAWBORAD_HAS_JPEG
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4611 4324)
#endif
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
void writeJpegMetadata(j_compress_ptr cinfo,const RasterEncodingOptions& o) {
    if(o.metadata.preserveExif && !o.metadata.exif.empty()) {
        if(o.metadata.exif.size()<=65533)
            jpeg_write_marker(cinfo,JPEG_APP0+1,o.metadata.exif.data(),unsigned(o.metadata.exif.size()));
    }
    if(o.metadata.preserveXmp && !o.metadata.xmp.empty()) {
        static constexpr char prefix[]="http://ns.adobe.com/xap/1.0/";
        std::vector<JOCTET> marker(sizeof(prefix)+o.metadata.xmp.size());
        std::memcpy(marker.data(),prefix,sizeof(prefix));
        std::memcpy(marker.data()+sizeof(prefix),o.metadata.xmp.data(),o.metadata.xmp.size());
        if(marker.size()<=65533)
            jpeg_write_marker(cinfo,JPEG_APP0+1,marker.data(),unsigned(marker.size()));
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
    writeJpegMetadata(&cinfo,o);
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
#ifdef _MSC_VER
#pragma warning(pop)
#endif
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
#ifdef TIFFTAG_XMLPACKET
    if(o.metadata.preserveXmp&&!o.metadata.xmp.empty())
        TIFFSetField(t,TIFFTAG_XMLPACKET,std::uint32_t(o.metadata.xmp.size()),const_cast<std::uint8_t*>(o.metadata.xmp.data()));
#endif
    if(o.metadata.preserveExif&&!o.metadata.exif.empty()){TIFFClose(t);return error(ErrorCode::UnsupportedFeature,"tiff.exif-write-unavailable");}
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
    if(!o.iccProfile.empty() || (o.metadata.preserveExif&&!o.metadata.exif.empty()) ||
       (o.metadata.preserveXmp&&!o.metadata.xmp.empty())){
        WebPData bitstream{out.data(),out.size()},assembled{nullptr,0};
        WebPMux* mux=WebPMuxCreate(&bitstream,1);if(!mux)return error(ErrorCode::IoError,"webp.mux");
        auto setChunk=[&](const char* fourcc,const Bytes& data)->bool{
            if(data.empty())return true;
            WebPData chunk{data.data(),data.size()};
            return WebPMuxSetChunk(mux,fourcc,&chunk,1)==WEBP_MUX_OK;
        };
        bool ok=setChunk("ICCP",o.iccProfile);
        if(ok&&o.metadata.preserveExif)ok=setChunk("EXIF",o.metadata.exif);
        if(ok&&o.metadata.preserveXmp)ok=setChunk("XMP ",o.metadata.xmp);
        auto assembledStatus=ok?WebPMuxAssemble(mux,&assembled):WEBP_MUX_INVALID_ARGUMENT;
        WebPMuxDelete(mux);
        if(assembledStatus!=WEBP_MUX_OK)return error(ErrorCode::IoError,"webp.metadata");
        out.assign(assembled.bytes,assembled.bytes+assembled.size);WebPDataClear(&assembled);
    }
#else
    if(!o.iccProfile.empty() || (o.metadata.preserveExif&&!o.metadata.exif.empty()) ||
       (o.metadata.preserveXmp&&!o.metadata.xmp.empty()))
        return error(ErrorCode::UnsupportedFeature,"webp.metadata-mux-unavailable");
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
    auto metadata=validateMetadata(options);if(!metadata)return metadata.error();
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
    static std::atomic<std::uint64_t> serial{};
    auto stagedPath=plan.request.path;
    stagedPath += std::string(".stage-") +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                  "-" + std::to_string(serial++);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}
    } cleanup{stagedPath};
    NativeFileIO io;
    auto written=io.write(stagedPath,encoded.value(),true);
    if(!written)return written.error();
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"export.cancelled");
    StagedExport staged{stagedPath,sha256(encoded.value())};
    DestinationPolicy policy{plan.request.path,plan.request.overwrite,true};
    auto committed=commitExport(io,staged,policy,2*GiB);
    if(!committed)return committed.error();
    ExportReport report;report.paths={plan.request.path};report.hashes={staged.verifiedHash};report.dimensions=plan.request.size;
    report.channels=colorChannels(pixels.format.mode)+(pixels.format.alpha?1u:0u);
    if(plan.requiresRgbConversion)report.warnings.push_back("explicit-rgb-conversion");
    return report;
}
}
