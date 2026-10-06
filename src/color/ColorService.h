#pragma once
#include "ProfileStore.h"
#include "core/Document.h"
namespace drawborad {
enum class ColorIntent:std::uint8_t{Perceptual,Relative,Absolute,Saturation};
struct ColorTransform{ProfileId src{},dst{};ColorIntent intent{ColorIntent::Relative};bool blackPointCompensation{true};std::uint32_t engineVersion{1};};
class ColorService{const ProfileStore&profiles_;public:explicit ColorService(const ProfileStore&p):profiles_(p){}Result<TileData> convert(const TileData&,const ColorTransform&)const;Result<ColorSpec> assign(const ColorSpec&,ProfileId)const;Result<ChangeSet> assign(Document&,ProfileId)const;bool hasNativeTransformBackend()const;};
}