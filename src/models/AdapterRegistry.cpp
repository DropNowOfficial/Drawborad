#include "AdapterRegistry.h"
#include <cmath>
namespace drawborad {
Result<AdapterInfo> AdapterRegistry::resolve(std::string_view task)const{if(task!="sam21.image.v1")return error(ErrorCode::UnsupportedAdapter,"adapter.unknown");AdapterInfo a;a.id="sam21.image.v1";a.abi="1";a.inputs={{"image","float32",{1,3,1024,1024},16*1024*1024},{"point_coords","float32",{1,-1,2},4096},{"point_labels","float32",{1,-1},2048}};a.outputs={{"low_res_masks","float32",{1,-1,256,256},8*1024*1024},{"iou_scores","float32",{1,-1},64}};return a;}
Result<CompatibilityReport> probeModel(const ModelManifest&m,DevicePreference d,std::stop_token st){if(st.stop_requested())return error(ErrorCode::Cancelled,"adapter.cancelled");AdapterRegistry r;auto a=r.resolve(m.taskId);if(!a)return a.error();if(m.provider=="cuda"&&d==DevicePreference::CPU)return CompatibilityReport{true,"cpu-fallback-explicit",m.ramBytes,0,d};if(d==DevicePreference::CUDA&&m.provider!="cuda")return CompatibilityReport{false,"cuda-not-qualified",0,0,d};return CompatibilityReport{true,"manifest-contract-valid",m.ramBytes,m.gpuBytes,d};}
}
