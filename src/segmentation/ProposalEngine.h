#pragma once
#include "models/AdapterRegistry.h"
#include "core/Document.h"
namespace drawborad {
struct AnalysisView { std::vector<TileData> rgbTiles; LayerTransform sourceTransform; std::vector<Bytes> sourceAlpha; Hash iccTransformHash{}; };
struct PromptPoint { double x{},y{}; bool positive{true}; };
struct PromptBox { double x0{},y0{},x1{},y1{}; };
struct PromptSet { std::vector<PromptPoint> points; std::vector<PromptBox> boxes; };
struct AnalysisRequest { PixelSize sourceSize{}; AnalysisView view; ModelManifest model; PromptSet prompts; Revision inputRevision{}; };
struct ProposalCandidate { Id id{}; Rect bounds{}; Bytes lowResMask; double score{}; };
struct ProposalSet {
    std::vector<ProposalCandidate> candidates;
    Hash modelHash{};
    Revision inputRevision{};
    PixelSize sourceSize{};
    LayerTransform sourceToAnalysis{};
    std::vector<Bytes> sourceAlpha;
};
struct MaskSet { PixelSize dimensions{}; std::vector<Bytes> fullResolutionMasks; Bytes unassigned; };
struct ProjectionPolicy { PixelSize targetSize{}; bool preserveAlpha{true}; };
class ProposalEngine { public: Result<ProposalSet> propose(const AnalysisRequest&,std::stop_token={}) const; };
Result<MaskSet> projectMasks(const ProposalSet&,const ProjectionPolicy&);
}
