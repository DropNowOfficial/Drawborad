#pragma once
#include "models/AdapterRegistry.h"
#include "core/Document.h"
#include <functional>
namespace drawborad {
struct AnalysisView { std::vector<TileData> rgbTiles; LayerTransform sourceTransform; std::vector<Bytes> sourceAlpha; Hash iccTransformHash{}; };
struct PromptPoint { double x{},y{}; bool positive{true}; };
struct PromptBox { double x0{},y0{},x1{},y1{}; };
struct PromptSet { std::vector<PromptPoint> points; std::vector<PromptBox> boxes; };
struct AnalysisRequest { PixelSize sourceSize{}; AnalysisView view; ModelManifest model; PromptSet prompts; Revision inputRevision{}; };
struct ProposalCandidate {
    Id id{}; Rect bounds{}; Bytes lowResMask; double score{};
    // Runtime SAM logits cover the complete 1024-pixel analysis frame. Legacy
    // manual masks instead use bounds-local coordinates and have no logits.
    std::vector<float> lowResLogits{};
};
struct ProposalSet {
    std::vector<ProposalCandidate> candidates;
    Hash modelHash{};
    Revision inputRevision{};
    PixelSize sourceSize{};
    LayerTransform sourceToAnalysis{};
    std::vector<Bytes> sourceAlpha;
};
struct MaskSet { PixelSize dimensions{}; std::vector<Bytes> fullResolutionMasks; Bytes unassigned; };
struct ProjectionPolicy { PixelSize targetSize{}; bool preserveAlpha{true}; float threshold{}; };
struct AutomaticProposalOptions {
    unsigned pointsPerSide{32};
    double predictedIouThreshold{0.8}, stabilityThreshold{0.95}, boxNmsThreshold{0.7};
    std::size_t maxCandidates{256};
};
using AnalysisProgress = std::function<void(unsigned completed, unsigned total)>;
using InferenceRunner = std::function<Result<InferenceOutput>(
    const ModelManifest&, const InferenceRequest&, std::stop_token)>;
struct ProposalEngineState;
class ProposalEngine {
    InferenceRunner runner_;
    bool forceRuntime_{};
    std::shared_ptr<ProposalEngineState> state_;
  public:
    ProposalEngine();
    explicit ProposalEngine(InferenceRunner);
    Result<ProposalSet> propose(const AnalysisRequest&,std::stop_token={}) const;
    Result<ProposalSet> proposeAutomatic(const AnalysisRequest&, const AutomaticProposalOptions& = {},
                                        std::stop_token = {}, AnalysisProgress = {}) const;
};
Result<MaskSet> projectMasks(const ProposalSet&,const ProjectionPolicy&);
}
