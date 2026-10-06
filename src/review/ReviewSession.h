#pragma once
#include "segmentation/ProposalEngine.h"
#include <limits>
#include <functional>
namespace drawborad {
enum class MaskEditOp { AddBrush, SubtractBrush, Feather, Grow, Shrink, Merge, Split, Transform, Lock, Unlock };
struct MaskEdit {
    MaskEditOp operation{MaskEditOp::AddBrush};
    std::size_t objectIndex{};
    Rect region{};
    double radiusPx{};
    Revision expectedRevision{};
    std::size_t secondaryIndex{std::numeric_limits<std::size_t>::max()};
    LayerTransform transform{};
};
struct ReviewedObjects { std::vector<Bytes> masks; Revision confirmedRevision{}; bool canReconstruct{}; };
using RefineRunner = std::function<Result<ProposalSet>(const PromptSet&, std::stop_token)>;
class ReviewSession {
    ProposalSet proposals_;
    MaskSet masks_;
    Revision revision_{};
    struct UndoState { ProposalSet proposals; MaskSet masks; std::vector<bool> locked; };
    std::vector<UndoState> undo_;
    std::vector<bool> locked_;
    RefineRunner refineRunner_;
  public:
    static Result<ReviewSession> create(const ProposalSet&,const MaskSet&,Revision,
                                        RefineRunner = {});
    Result<void> apply(const MaskEdit&);
    Result<void> undo();
    Result<ProposalSet> refine(const PromptSet&,std::stop_token={}) const;
    Result<ReviewedObjects> accept(Revision) const;
    const MaskSet& masks() const{return masks_;}
    Revision revision() const{return revision_;}
};
}
