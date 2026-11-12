#pragma once
#include "MergedDocument.h"
#include "core/MyPaintCroppedRaster.h"
#include "codecs/LayerArchive.h"
namespace drawborad {
// Front-to-back leaf stream. Groups are flattened, ancestor visibility and masks
// baked into leaf exports; this is not editable group/mask round-trip fidelity.
// Snapshot pixels/resources are borrowed immutably. Only read()'s returned layer
// is allocated full-size; caller must release it before requesting the next one.
class NativeLayerStream {
 DocumentSnapshot snapshot_;
 EventLimit limit_;
 std::vector<NodeId> order_;
 ReferenceRenderer renderer_;
 croppedRaster::PreparedViews croppedViews_;
 public:
 static Result<NativeLayerStream> create(const DocumentSnapshot&,EventLimit,MergedBudget={});
 std::size_t size()const{return order_.size();}
 Result<ArchiveLayer> read(std::size_t,std::stop_token={});
 RenderStatistics statistics()const{return renderer_.statistics();}
};
}
