#include "AdapterRegistry.h"
#include <cmath>
namespace drawborad {
Result<AdapterInfo> AdapterRegistry::resolve(std::string_view task)const{if(task!="sam21.image.v1")return error(ErrorCode::UnsupportedAdapter,"adapter.unknown");AdapterInfo a;a.id="sam21.image.v1";a.abi="1";a.inputs={{"image","float32",{1,3,1024,1024},16*1024*1024},{"point_coords","float32",{1,-1,2},4096},{"point_labels","float32",{1,-1},2048}};a.outputs={{"low_res_masks","float32",{1,-1,256,256},8*1024*1024},{"iou_scores","float32",{1,-1},64}};return a;}
Result<CompatibilityReport> probeModel(const ModelManifest&m,DevicePreference d,std::stop_token st){if(st.stop_requested())return error(ErrorCode::Cancelled,"adapter.cancelled");AdapterRegistry r;auto a=r.resolve(m.taskId);if(!a)return a.error();
 auto compatible=[](const TensorSpec& got,const TensorSpec& expected){if(got.name!=expected.name||got.dtype!=expected.dtype||got.shape.size()!=expected.shape.size())return false;for(std::size_t i=0;i<got.shape.size();++i)if(expected.shape[i]>=0&&got.shape[i]!=expected.shape[i])return false;return got.byteLimit<=expected.byteLimit;};
 if(!m.inputs.empty()){if(m.inputs.size()!=a.value().inputs.size())return CompatibilityReport{false,"input-contract-mismatch",0,0,d};for(std::size_t i=0;i<m.inputs.size();++i)if(!compatible(m.inputs[i],a.value().inputs[i]))return CompatibilityReport{false,"input-contract-mismatch",0,0,d};}
 if(!m.outputs.empty()){if(m.outputs.size()!=a.value().outputs.size())return CompatibilityReport{false,"output-contract-mismatch",0,0,d};for(std::size_t i=0;i<m.outputs.size();++i)if(!compatible(m.outputs[i],a.value().outputs[i]))return CompatibilityReport{false,"output-contract-mismatch",0,0,d};}
 if(m.provider=="cuda"&&d==DevicePreference::CPU)return CompatibilityReport{true,"cpu-fallback-explicit",m.ramBytes,0,d};
 if(d==DevicePreference::CUDA&&m.provider!="cuda")return CompatibilityReport{false,"cuda-not-qualified",0,0,d};
 return CompatibilityReport{true,"manifest-contract-valid",m.ramBytes,m.gpuBytes,d};
}
}
