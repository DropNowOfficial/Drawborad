#include "ModelManifest.h"
namespace drawborad {
Result<ModelId> ModelRegistry::install(const PackagePath&p,TrustDecision trust){auto m=validatePackage(p);if(!m)return m.error();if(!m.value().signedPackage&&trust!=TrustDecision::UserAcceptedUntrusted)return error(ErrorCode::LicenseBlocked,"model.untrusted-consent");ModelId id;std::copy_n(m.value().sha256.bytes.begin(),16,id.bytes.begin());if(installed_.contains(id))return id;installed_[id]=m.value();roots_[id]=p;return id;}
Result<void> ModelRegistry::remove(ModelId id){if(active_[id])return error(ErrorCode::Locked,"model.active");if(!installed_.erase(id))return error(ErrorCode::InvalidInput,"model.not-installed");roots_.erase(id);active_.erase(id);return{};}
Result<ModelManifest> ModelRegistry::manifest(ModelId id)const{auto it=installed_.find(id);if(it==installed_.end())return error(ErrorCode::InvalidInput,"model.not-installed");return it->second;}
Result<void> ModelRegistry::retain(ModelId id){if(!installed_.contains(id))return error(ErrorCode::InvalidInput,"model.not-installed");active_[id]++;return{};}
void ModelRegistry::release(ModelId id){auto it=active_.find(id);if(it!=active_.end()&&it->second)--it->second;}
}
