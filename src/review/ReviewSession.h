#pragma once
#include "segmentation/ProposalEngine.h"
namespace drawborad {
enum class MaskEditOp { AddBrush, SubtractBrush, Feather, Grow, Shrink, Merge, Split, Transform, Lock, Unlock };
struct MaskEdit { MaskEditOp operation{MaskEditOp::AddBrush}; std::size_t objectIndex{}; Rect region{}; double radiusPx{}; Revision expectedRevision{}; };
struct ReviewedObjects { std::vector<Bytes> masks; Revision confirmedRevision{}; bool canReconstruct{}; };
class ReviewSession {
    ProposalSet proposals_;
    MaskSet masks_;
    Revision revision_{};
    std::vector<MaskSet> undo_;
    std::vector<bool> locked_;
  public:
    static Result<ReviewSession> create(const ProposalSet&,const MaskSet&,Revision);
    Result<void> apply(const MaskEdit&);
    Result<void> undo();
    Result<ReviewedObjects> accept(Revision) const;
    const MaskSet& masks() const{return masks_;}
    Revision revision() const{return revision_;}
};
}
