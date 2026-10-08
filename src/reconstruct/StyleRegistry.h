#pragma once
#include "core/Document.h"
#include <filesystem>
#include <string>
namespace drawborad {
struct ReconstructionStyle {
    std::string id;
    std::uint32_t schemaVersion{};
    std::string tool;
    double widthScale{},opacityScale{},hardness{},grainStrength{},pressureVariation{};
    bool fidelity{};
    std::string canonicalJson;
};
struct StyleVariant {
    DocumentSnapshot snapshot;
    ReconstructionStyle style;
    std::uint64_t seed{};
    Hash provenanceResource{};
};
class StyleRegistry {
    std::filesystem::path root_;
public:
    explicit StyleRegistry(std::filesystem::path root) : root_(std::move(root)) {}
    Result<ReconstructionStyle> load(std::string_view styleId) const;
};
Result<StyleVariant> makeStyleVariant(const DocumentSnapshot&,
                                    const ReconstructionStyle&,std::uint64_t seed);
}
