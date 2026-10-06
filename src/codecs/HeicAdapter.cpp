#include "HeicAdapter.h"
#include "core/Digest.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#ifdef DRAWBORAD_HAS_HEIF
#include <libheif/heif.h>
#endif

namespace drawborad {
namespace {
bool brand(std::span<const std::uint8_t> bytes, std::size_t at) {
    if (at + 4 > bytes.size()) return false;
    static constexpr std::array<std::array<char,4>,8> brands{{
        {{'h','e','i','c'}}, {{'h','e','i','x'}}, {{'h','e','v','c'}}, {{'h','e','v','x'}},
        {{'h','e','i','m'}}, {{'h','e','i','s'}}, {{'m','i','f','1'}}, {{'m','s','f','1'}}
    }};
    for (const auto& b : brands)
        if (std::memcmp(bytes.data()+at,b.data(),4)==0) return true;
    return false;
}
std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|
           (std::uint32_t(p[2])<<8)|std::uint32_t(p[3]);
}
#ifdef DRAWBORAD_HAS_HEIF
struct Context {
    heif_context* value{heif_context_alloc()};
    ~Context(){ if(value) heif_context_free(value); }
};
struct Handle {
    heif_image_handle* value{};
    Handle()=default;
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value,nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if(this!=&other){if(value)heif_image_handle_release(value);value=std::exchange(other.value,nullptr);}
        return *this;
    }
    ~Handle(){ if(value) heif_image_handle_release(value); }
};
struct Image {
    heif_image* value{};
    ~Image(){ if(value) heif_image_release(value); }
};
Result<void> readMemory(heif_context* ctx,std::span<const std::uint8_t> bytes) {
    auto e=heif_context_read_from_memory_without_copy(ctx,bytes.data(),bytes.size(),nullptr);
    if(e.code!=heif_error_Ok)
        return error(ErrorCode::CorruptData,"heic.read");
    return {};
}
Result<Handle> selectHandle(heif_context* ctx,std::uint32_t frame) {
    const int count=heif_context_get_number_of_top_level_images(ctx);
    if(count<=0) return error(ErrorCode::CorruptData,"heic.no-images");
    if(frame>=std::uint32_t(count)) return error(ErrorCode::InvalidInput,"heic.frame");
    std::vector<heif_item_id> ids;
    ids.resize(std::size_t(count));
    const int got=heif_context_get_list_of_top_level_image_IDs(ctx,ids.data(),count);
    if(got!=count) return error(ErrorCode::CorruptData,"heic.image-list");
    Handle h;
    auto e=heif_context_get_image_handle(ctx,ids[frame],&h.value);
    if(e.code!=heif_error_Ok||!h.value)
        return error(ErrorCode::CorruptData,"heic.image-handle");
    return h;
}
#endif
}

bool heicCodecAvailable() {
#ifdef DRAWBORAD_HAS_HEIF
    return true;
#else
    return false;
#endif
}

bool looksLikeHeic(std::span<const std::uint8_t> bytes) {
    if(bytes.size()<16 || std::memcmp(bytes.data()+4,"ftyp",4)!=0) return false;
    auto box=std::uint64_t(be32(bytes.data()));
    if(box<16 || box>bytes.size()) box=bytes.size();
    if(brand(bytes,8)) return true;
    for(std::size_t at=16;at+4<=box;at+=4)
        if(brand(bytes,at)) return true;
    return false;
}

Result<InputInfo> inspectHeic(std::span<const std::uint8_t> bytes,const CodecLimits& limits) {
    if(!looksLikeHeic(bytes))
        return error(ErrorCode::UnsupportedFeature,"heic.magic");
#ifndef DRAWBORAD_HAS_HEIF
    (void)limits;
    return error(ErrorCode::UnsupportedFeature,"heic.codec-unavailable");
#else
    Context ctx;
    if(!ctx.value) return error(ErrorCode::IoError,"heic.context");
    auto read=readMemory(ctx.value,bytes); if(!read) return read.error();
    auto selected=selectHandle(ctx.value,0); if(!selected) return selected.error();
    auto& h=selected.value();
    const int width=heif_image_handle_get_width(h.value);
    const int height=heif_image_handle_get_height(h.value);
    if(width<=0||height<=0) return error(ErrorCode::CorruptData,"heic.dimensions");
    auto pixels=checkedMultiply(std::uint64_t(width),std::uint64_t(height));
    if(!pixels) return pixels.error();
    auto decoded=checkedMultiply(pixels.value(),4);
    if(!decoded||decoded.value()>limits.decodedBytes)
        return error(ErrorCode::BudgetExceeded,"heic.decode-budget");
    InputInfo info;
    info.format=ImageFormat::Heic;
    info.originalSize={std::uint64_t(width),std::uint64_t(height)};
    info.orientedSize=info.originalSize;
    info.hasAlpha=heif_image_handle_has_alpha_channel(h.value)!=0;
    info.estimatedBytes=decoded.value();
    info.sourceHash=sha256(bytes);
    const auto profileSize=heif_image_handle_get_raw_color_profile_size(h.value);
    if(profileSize>0 && profileSize<=32ull*1024*1024) {
        Bytes profile(profileSize);
        auto e=heif_image_handle_get_raw_color_profile(h.value,profile.data());
        if(e.code==heif_error_Ok){info.iccProfile=profile;info.icc=sha256(info.iccProfile);}
    }
    return info;
#endif
}

Result<TileData> decodeHeic(std::span<const std::uint8_t> bytes,InputInfo& info,
                            const DecodeOptions& options,std::stop_token stop) {
    if(stop.stop_requested()) return error(ErrorCode::Cancelled,"heic.cancelled");
#ifndef DRAWBORAD_HAS_HEIF
    (void)bytes;(void)info;(void)options;
    return error(ErrorCode::UnsupportedFeature,"heic.codec-unavailable");
#else
    if(!looksLikeHeic(bytes)) return error(ErrorCode::UnsupportedFeature,"heic.magic");
    Context ctx;
    if(!ctx.value) return error(ErrorCode::IoError,"heic.context");
    auto read=readMemory(ctx.value,bytes); if(!read) return read.error();
    auto selected=selectHandle(ctx.value,options.frame); if(!selected) return selected.error();
    auto& h=selected.value();
    const int width=heif_image_handle_get_width(h.value);
    const int height=heif_image_handle_get_height(h.value);
    if(width<=0||height<=0) return error(ErrorCode::CorruptData,"heic.dimensions");
    const bool alpha=heif_image_handle_has_alpha_channel(h.value)!=0;
    const unsigned channels=alpha?4u:3u;
    auto row=checkedMultiply(std::uint64_t(width),channels);
    if(!row) return row.error();
    auto total=checkedMultiply(row.value(),std::uint64_t(height));
    if(!total||total.value()>options.limits.decodedBytes||total.value()>SIZE_MAX)
        return error(ErrorCode::BudgetExceeded,"heic.decode-budget");
    if(stop.stop_requested()) return error(ErrorCode::Cancelled,"heic.cancelled");
    Image image;
    const auto chroma=alpha?heif_chroma_interleaved_RGBA:heif_chroma_interleaved_RGB;
    auto e=heif_decode_image(h.value,&image.value,heif_colorspace_RGB,chroma,nullptr);
    if(e.code!=heif_error_Ok||!image.value)
        return error(ErrorCode::CorruptData,"heic.decode");
    int stride=0;
    const auto* plane=heif_image_get_plane_readonly(image.value,heif_channel_interleaved,&stride);
    if(!plane||stride<int(row.value()))
        return error(ErrorCode::CorruptData,"heic.stride");
    TileData tile;
    tile.width=std::uint32_t(width);
    tile.height=std::uint32_t(height);
    tile.format={ColorMode::RGB,Depth::U8,alpha};
    tile.profileId=options.explicitProfile.empty()?info.icc:options.explicitProfile;
    tile.rowStride=row.value();
    tile.bytes.resize(std::size_t(total.value()));
    for(int y=0;y<height;y++) {
        if(stop.stop_requested()) return error(ErrorCode::Cancelled,"heic.cancelled");
        std::memcpy(tile.bytes.data()+std::size_t(y*tile.rowStride),
                    plane+std::size_t(y*stride),std::size_t(tile.rowStride));
    }
    info.format=ImageFormat::Heic;
    info.originalSize={std::uint64_t(width),std::uint64_t(height)};
    info.orientedSize=info.originalSize;
    info.hasAlpha=alpha;
    info.estimatedBytes=total.value();
    info.sourceHash=sha256(bytes);
    return tile;
#endif
}
}
