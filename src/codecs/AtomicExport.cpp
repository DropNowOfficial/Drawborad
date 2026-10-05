#include "CodecLimits.h"
#include "storage/FileIO.h"
namespace drawborad {
struct StagedExport { std::filesystem::path tempPath; Hash verifiedHash{}; };
struct DestinationPolicy { std::filesystem::path destination; bool replaceConfirmed{}; bool atomic{true}; };
Result<void> commitExport(const StagedExport&s,const DestinationPolicy&p){if(s.tempPath.empty()||p.destination.empty())return error(ErrorCode::InvalidInput,"export.commit-path");if(!p.replaceConfirmed&&std::filesystem::exists(p.destination))return error(ErrorCode::InvalidInput,"export.replace-consent");NativeFileIO io;auto bytes=io.read(s.tempPath,2*GiB);if(!bytes)return bytes.error();if(sha256(bytes.value())!=s.verifiedHash)return error(ErrorCode::DigestMismatch,"export.staged-hash");return atomicWrite(io,p.destination,bytes.value());}
}
