#include "ModelManifest.h"
namespace drawborad {
bool OrtSession::runtimeAvailable() const {
#ifdef DRAWBORAD_HAS_ONNXRUNTIME
 return true;
#else
 return false;
#endif
}
Result<InferenceOutput> OrtSession::run(const InferenceRequest&q,std::stop_token st)const{if(st.stop_requested())return error(ErrorCode::Cancelled,"model.cancelled");if(q.modelId.empty())return error(ErrorCode::InvalidInput,"model.id");if(!runtimeAvailable())return error(ErrorCode::UnsupportedFeature,"model.onnxruntime-unavailable");if(manifest_.taskId!="sam21.image.v1")return error(ErrorCode::UnsupportedAdapter,"model.task");return error(ErrorCode::UnsupportedFeature,"model.runtime-adapter-not-linked");}
}
