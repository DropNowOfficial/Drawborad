#pragma once
#include "Document.h"
#include <algorithm>
#include <string_view>

namespace drawborad {

// Unified single opt-in flags stored in InputSample::capabilities (already part of
// StrokeEvent binary wire format) and LayerNode::stage, consolidated under one
// format generation (DRAW-DELTA-26) so legacy .draw files remain byte-identical.
inline constexpr std::uint32_t StrokeCapabilityLinearOptical = 1u << 16;
inline constexpr std::uint32_t StrokeCapabilityPhysicalDryInk = 1u << 17;
inline constexpr std::uint32_t StrokeCapabilityArtistMask =
    StrokeCapabilityLinearOptical | StrokeCapabilityPhysicalDryInk;

inline bool hasLinearOpticalBlend(const StrokeEvent& e) {
    return !e.geometry.empty() &&
           (e.geometry.front().capabilities & StrokeCapabilityLinearOptical) != 0;
}

inline bool hasPhysicalDryInk(const StrokeEvent& e) {
    return !e.geometry.empty() &&
           (e.geometry.front().capabilities & StrokeCapabilityPhysicalDryInk) != 0;
}

inline bool isArtistStage(std::string_view stage) {
    return stage == "artist.flats" || stage == "artist.form_glaze" ||
           stage == "artist.structure" || stage == "artist.hero_ink" ||
           stage == "artist.highlights" || stage == "artist.linear_composite";
}

inline bool isLinearCompositeStage(std::string_view stage) {
    return stage == "artist.flats" || stage == "artist.structure" ||
           stage == "artist.hero_ink" || stage == "artist.linear_composite";
}

inline bool requiresArtistFormat26(const DocumentSnapshot& s) {
    if (s.nodes && std::any_of(s.nodes->begin(), s.nodes->end(), [](const auto& item) {
            return isArtistStage(item.second.stage);
        })) {
        return true;
    }
    if (s.eventIndex && std::any_of(s.eventIndex->begin(), s.eventIndex->end(), [](const auto& e) {
            return std::any_of(e.geometry.begin(), e.geometry.end(), [](const auto& p) {
                return (p.capabilities & StrokeCapabilityArtistMask) != 0;
            });
        })) {
        return true;
    }
    return false;
}

} // namespace drawborad
