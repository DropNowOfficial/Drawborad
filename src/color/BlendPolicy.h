#pragma once
#include "ColorService.h"
#include "core/Document.h"
namespace drawborad {
struct BlendSupport{bool enabled{};Hash evidenceHash{};};struct BlendRequest{TileData src,dst;BlendMode mode{BlendMode::Normal};double opacity{1};};struct ProofRequest{TileData tile;ColorTransform documentToProof,proofToDisplay;bool paperWhite{},gamutWarning{};};
BlendSupport supportedBlend(PixelFormat,BlendMode);Result<TileData> blendPixels(const BlendRequest&);Result<TileData> softProof(const ProofRequest&,const ColorService&);
}