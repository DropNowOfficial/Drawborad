#pragma once
#include "Planning.h"
#include <array>
#include <span>
#include <stop_token>
#include <vector>

namespace drawborad {

struct ArtistStageLayers {
    NodeId flats{};
    NodeId formGlaze{};
    NodeId structure{};
    NodeId heroInk{};
    NodeId highlights{};
};

struct ArtistObjectRequest {
    DocumentSnapshot snapshot;
    NodeId objectId{};
    ArtistStageLayers layers{};
    std::span<const std::uint8_t> sourceMask; // Binary 0/255 mask in source.width x source.height
    const TileData* source{};                 // RGBA8 source (or amodal-completed background source)
    PixelSize targetSize{};                   // Defaults to snapshot.spec.pixelSize; supports super-res (e.g. 3300x5100)
    bool clipToObjectMask{true};              // True when object.maskId is installed at targetSize
    std::uint64_t seed{1};
    std::uint64_t maxTotalEvents{8192};
    std::uint64_t maxTotalSamples{400000};
    std::stop_token stop{};
};

struct ArtistObjectPlan {
    std::vector<EventBatch> batches; // Ordered bottom-to-top across non-empty artist stage layers
};

// Purely in-memory multi-scale pull-push harmonic background completion for occluded
// foreground regions. Never persisted as a document resource (sourceAssetReads == 0).
Result<TileData> inpaintAmodalBackground(const TileData& source,
                                         std::span<const std::uint8_t> unoccludedMask,
                                         std::stop_token stop = {});

Result<ArtistObjectPlan> planArtistObject(const ArtistObjectRequest& request);

} // namespace drawborad
