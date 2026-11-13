#include "ZipStream.h"
#include <algorithm>
namespace drawborad {
namespace {
void u16(Bytes& b,std::uint16_t v){b.push_back(std::uint8_t(v));b.push_back(std::uint8_t(v>>8));}
void u32(Bytes& b,std::uint32_t v){u16(b,std::uint16_t(v));u16(b,std::uint16_t(v>>16));}
std::uint32_t crc(std::uint32_t value,std::span<const std::uint8_t> data){
 for(auto byte:data){value^=byte;for(unsigned i=0;i<8;i++)value=(value>>1)^((0u-(value&1u))&0xedb88320u);}return value;
}
bool validName(std::string_view name){
 if(name.empty() || name.front()=='/' || name.back()=='/' || name.find_first_of("\\:")!=name.npos)return false;
 for(std::size_t pos=0;pos<name.size();){
  auto end=name.find('/',pos);if(end==name.npos)end=name.size();auto part=name.substr(pos,end-pos);
  if(part.empty() || part=="." || part=="..")return false;
  pos=end+1;
 }
 for(std::size_t i=0;i<name.size();){
  auto c=static_cast<unsigned char>(name[i++]);if(c<32 || c==127)return false;if(c<128)continue;
  unsigned n=0;std::uint32_t code=0,min=0;
  if(c>=0xc2 && c<=0xdf){n=1;code=c&31;min=0x80;}
  else if(c>=0xe0 && c<=0xef){n=2;code=c&15;min=0x800;}
  else if(c>=0xf0 && c<=0xf4){n=3;code=c&7;min=0x10000;}else return false;
  if(n>name.size()-i)return false;
  while(n--){auto next=static_cast<unsigned char>(name[i++]);if((next&0xc0)!=0x80)return false;code=(code<<6)|(next&63);}
  if(code<min || code>0x10ffff || (code>=0xd800 && code<=0xdfff))return false;
 }
 return true;
}
}
ZipStream::ZipStream(Sink sink,ZipStreamLimits limits,std::stop_token stop):sink_(std::move(sink)),limits_(limits),stop_(stop){}
Result<void> ZipStream::fail(Error e){if(!failure_)failure_=std::move(e);return *failure_;}
Result<void> ZipStream::ready(){
 if(failure_)return *failure_;
 if(stop_.stop_requested())return fail(error(ErrorCode::Cancelled,"zip.cancelled"));
 if(finished_)return fail(error(ErrorCode::InvalidInput,"zip.finished"));
 if(!sink_)return fail(error(ErrorCode::InvalidInput,"zip.sink"));
 return {};
}
Result<void> ZipStream::emitBytes(std::span<const std::uint8_t> data){
 auto ok=ready();if(!ok)return ok;
 const auto cap=std::min<std::uint64_t>(limits_.maxBytes,UINT32_MAX);
 if(offset_>cap || data.size()>cap-offset_)return fail(error(ErrorCode::BudgetExceeded,"zip.byte-limit"));
 for(std::size_t at=0;at<data.size();){
  ok=ready();if(!ok)return ok;
  const auto count=std::min<std::size_t>(65536,data.size()-at);auto chunk=data.subspan(at,count);
  auto written=sink_(chunk);if(!written)return fail(written.error());offset_+=count;at+=count;
 }
 return ready();
}
Result<void> ZipStream::start(std::string name,std::uint16_t flags,std::uint32_t size,std::uint32_t checksum){
 auto ok=ready();if(!ok)return ok;if(open_)return fail(error(ErrorCode::InvalidInput,"zip.entry-open"));
 if(entries_.size()>=std::min<std::uint32_t>(limits_.maxEntries,65535))return fail(error(ErrorCode::BudgetExceeded,"zip.entry-limit"));
 if(name.size()>std::min<std::uint32_t>(limits_.maxNameBytes,65535))return fail(error(ErrorCode::BudgetExceeded,"zip.name-limit"));
 if(!validName(name))return fail(error(ErrorCode::PathEscape,"zip.entry-name"));
 for(const auto& entry:entries_)if(entry.name==name)return fail(error(ErrorCode::InvalidInput,"zip.duplicate-name"));
 // Conservative allowance for vector capacity, owned name and header scratch.
 const auto cost=std::uint64_t(name.size())*3+4*sizeof(Entry)+128;
 if(metadata_>limits_.maxMetadataBytes || cost>limits_.maxMetadataBytes-metadata_)return fail(error(ErrorCode::BudgetExceeded,"zip.metadata-limit"));
 current_={std::move(name),std::uint32_t(offset_),size,checksum,flags};
 Bytes header;u32(header,0x04034b50);u16(header,20);u16(header,flags);u16(header,0);u16(header,0);u16(header,0x21);
 u32(header,checksum);u32(header,size);u32(header,size);u16(header,std::uint16_t(current_.name.size()));u16(header,0);
 header.insert(header.end(),current_.name.begin(),current_.name.end());
 ok=emitBytes(header);if(!ok)return ok;metadata_+=cost;open_=true;crc_=0xffffffffu;return {};
}
Result<void> ZipStream::addKnown(std::string name,std::span<const std::uint8_t> data) try {
 auto ok=ready();if(!ok)return ok;
 if(data.size()>UINT32_MAX)return fail(error(ErrorCode::BudgetExceeded,"zip.entry-size"));
 auto checksum=std::uint32_t(0xffffffffu);
 for(std::size_t at=0;at<data.size();){ok=ready();if(!ok)return ok;const auto n=std::min<std::size_t>(65536,data.size()-at);checksum=crc(checksum,data.subspan(at,n));at+=n;}
 ok=start(std::move(name),0x0800,std::uint32_t(data.size()),~checksum);if(!ok)return ok;
 ok=emitBytes(data);if(!ok)return ok;entries_.push_back(std::move(current_));open_=false;return {};
}catch(const std::bad_alloc&){return fail(error(ErrorCode::BudgetExceeded,"zip.allocation"));}catch(...){return fail(error(ErrorCode::IoError,"zip.sink-exception"));}
Result<void> ZipStream::begin(std::string name) try {return start(std::move(name),0x0808,0,0);}
catch(const std::bad_alloc&){return fail(error(ErrorCode::BudgetExceeded,"zip.allocation"));}catch(...){return fail(error(ErrorCode::IoError,"zip.sink-exception"));}
Result<void> ZipStream::write(std::span<const std::uint8_t> data) try {
 auto ok=ready();if(!ok)return ok;if(!open_ || current_.flags!=0x0808)return fail(error(ErrorCode::InvalidInput,"zip.no-stream-entry"));
 if(data.size()>UINT32_MAX-current_.size)return fail(error(ErrorCode::BudgetExceeded,"zip.entry-size"));
 for(std::size_t at=0;at<data.size();){
  const auto n=std::min<std::size_t>(65536,data.size()-at);auto chunk=data.subspan(at,n);
  ok=emitBytes(chunk);if(!ok)return ok;crc_=crc(crc_,chunk);current_.size+=std::uint32_t(n);at+=n;
 }return {};
}catch(const std::bad_alloc&){return fail(error(ErrorCode::BudgetExceeded,"zip.allocation"));}catch(...){return fail(error(ErrorCode::IoError,"zip.sink-exception"));}
Result<void> ZipStream::end() try {
 auto ok=ready();if(!ok)return ok;if(!open_)return fail(error(ErrorCode::InvalidInput,"zip.no-entry"));
 current_.crc=~crc_;Bytes descriptor;u32(descriptor,0x08074b50);u32(descriptor,current_.crc);u32(descriptor,current_.size);u32(descriptor,current_.size);
 ok=emitBytes(descriptor);if(!ok)return ok;entries_.push_back(std::move(current_));open_=false;return {};
}catch(const std::bad_alloc&){return fail(error(ErrorCode::BudgetExceeded,"zip.allocation"));}catch(...){return fail(error(ErrorCode::IoError,"zip.sink-exception"));}
Result<void> ZipStream::finish() try {
 auto ok=ready();if(!ok)return ok;if(open_)return fail(error(ErrorCode::InvalidInput,"zip.entry-unfinished"));
 std::uint64_t centralSize=0;for(const auto& e:entries_)centralSize+=46+e.name.size();
 const auto cap=std::min<std::uint64_t>(limits_.maxBytes,UINT32_MAX);
 if(offset_>cap || centralSize+22>cap-offset_)return fail(error(ErrorCode::BudgetExceeded,"zip.central-limit"));
 const auto centralOffset=std::uint32_t(offset_);
 for(const auto& e:entries_){
  Bytes header;u32(header,0x02014b50);u16(header,20);u16(header,20);u16(header,e.flags);u16(header,0);u16(header,0);u16(header,0x21);
  u32(header,e.crc);u32(header,e.size);u32(header,e.size);u16(header,std::uint16_t(e.name.size()));u16(header,0);u16(header,0);u16(header,0);u16(header,0);u32(header,0);u32(header,e.offset);
  header.insert(header.end(),e.name.begin(),e.name.end());ok=emitBytes(header);if(!ok)return ok;
 }
 Bytes end;u32(end,0x06054b50);u16(end,0);u16(end,0);u16(end,std::uint16_t(entries_.size()));u16(end,std::uint16_t(entries_.size()));u32(end,std::uint32_t(centralSize));u32(end,centralOffset);u16(end,0);
 ok=emitBytes(end);if(!ok)return ok;finished_=true;return {};
}catch(const std::bad_alloc&){return fail(error(ErrorCode::BudgetExceeded,"zip.allocation"));}catch(...){return fail(error(ErrorCode::IoError,"zip.sink-exception"));}
}
