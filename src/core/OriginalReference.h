#pragma once
#include "Commands.h"
#include "Digest.h"
#include <algorithm>
namespace drawborad {
inline constexpr std::uint8_t OriginalReferenceMagic[]={'D','B','O','R','I','G','0','1'};
struct OriginalReference { Hash encoded; std::uint32_t width{},height{}; };
// Non-drawing project attachment. No node, timeline or canvas geometry changes.
inline Result<std::optional<OriginalReference>> originalReference(const DocumentSnapshot& s,bool verifyDigest=true) {
 std::optional<OriginalReference> found;
 if(!s.resources)return error(ErrorCode::CorruptData,"reference.resources");
 for(const auto& [id,r]:*s.resources){
  if(!r.bytes||r.bytes->size()<8||!std::equal(std::begin(OriginalReferenceMagic),std::end(OriginalReferenceMagic),r.bytes->begin()))continue;
  if(found||r.kind!=ResourceKind::Reference||r.bytes->size()!=16||r.dependencies.size()!=1||sha256(*r.bytes)!=id)return error(ErrorCode::CorruptData,"reference.marker");
  const auto read=[&](unsigned p){std::uint32_t v=0;for(unsigned i=0;i<4;i++)v|=std::uint32_t((*r.bytes)[p+i])<<(8*i);return v;};
  OriginalReference ref{r.dependencies[0],read(8),read(12)};
  if(!ref.width||!ref.height||ref.width>8192||ref.height>8192||std::uint64_t(ref.width)*ref.height>20000000)return error(ErrorCode::BudgetExceeded,"reference.dimensions");
  const auto image=s.resources->find(ref.encoded);
  if(image==s.resources->end()||image->second.kind!=ResourceKind::Reference||!image->second.bytes||image->second.bytes->empty()||image->second.bytes->size()>32*1024*1024||!image->second.dependencies.empty()||(verifyDigest&&sha256(*image->second.bytes)!=ref.encoded))return error(ErrorCode::CorruptData,"reference.encoded");
  found=ref;
 }
 return found;
}
inline Result<Command> attachOriginalReference(const DocumentSnapshot& s,Bytes encoded,std::uint32_t width,std::uint32_t height) try {
 auto old=originalReference(s);if(!old)return old.error();
 if(encoded.empty()||encoded.size()>32*1024*1024||!width||!height||width>8192||height>8192||std::uint64_t(width)*height>20000000)return error(ErrorCode::BudgetExceeded,"reference.budget");
 std::uint64_t total=encoded.size()+16;for(const auto& [id,r]:*s.resources){(void)id;if(!r.bytes||r.bytes->size()>MaxLargeDocumentResourceBytes-total)return error(ErrorCode::BudgetExceeded,"reference.total-resource-budget");total+=r.bytes->size();}
 Resource image;image.kind=ResourceKind::Reference;image.id=sha256(encoded);image.bytes=std::make_shared<const Bytes>(std::move(encoded));
 if(old.value())return error(ErrorCode::InvalidInput,"reference.already-attached");
 Bytes b(std::begin(OriginalReferenceMagic),std::end(OriginalReferenceMagic));for(auto n:{width,height})for(unsigned i=0;i<4;i++)b.push_back(std::uint8_t(n>>(8*i)));
 Resource marker;marker.kind=ResourceKind::Reference;marker.id=sha256(b);marker.bytes=std::make_shared<const Bytes>(std::move(b));marker.dependencies={image.id};
 Command c;c.operations={InstallResource{image},InstallResource{marker}};return c;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"reference.allocation");}
}
