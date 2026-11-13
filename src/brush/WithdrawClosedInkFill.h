#pragma once
#include "ClosedInkFill.h"
#include "core/EditableStroke.h"
namespace drawborad {
// Regenerates the exact producer command. Hash consistency alone does not
// authorize withdrawal. This returns a temporary candidate, never mutates s.
inline Result<DocumentSnapshot> closedInkWithdrawalCandidate(const DocumentSnapshot&s,Id operationId,std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"fill-withdraw.cancelled");
 auto valid=validateSnapshot(s);if(!valid)return valid.error();
 const auto found=std::find_if(s.fillOperations.begin(),s.fillOperations.end(),[&](const auto&o){return o.id==operationId;});
 if(found==s.fillOperations.end())return error(ErrorCode::InvalidInput,"fill-withdraw.record-required");
 const auto&record=*found;if(fill_provenance::contextDigest(s,record)!=record.contextDigest)return error(ErrorCode::UnsupportedFeature,"fill-withdraw.context-changed");
 const auto first=std::find_if(s.eventIndex->begin(),s.eventIndex->end(),[&](const auto&e){return e.id==record.members.front().first;});
 if(first==s.eventIndex->end())return error(ErrorCode::InvalidInput,"fill-withdraw.missing-member");
 const auto begin=std::size_t(first-s.eventIndex->begin()),end=begin+record.members.size();
 if(end>s.eventIndex->size()||first->parentRevision>=s.revision)return error(ErrorCode::InvalidInput,"fill-withdraw.creation-revision");
 std::set<Id> memberIds;for(std::size_t i=0;i<record.members.size();++i){const auto&e=(*s.eventIndex)[begin+i];if(e.id!=record.members[i].first||e.parentRevision!=first->parentRevision)return error(ErrorCode::InvalidInput,"fill-withdraw.noncontiguous-group");memberIds.insert(e.id);}
 for(std::size_t i=end;i<s.eventIndex->size();++i){auto check=independentNativeStroke(s,(*s.eventIndex)[i],stop);if(!check){if(check.error().code==ErrorCode::Cancelled)return check.error();return error(ErrorCode::UnsupportedFeature,"fill-withdraw.later-dependent-event");}}
 // No other recorded operation may reach the group or its retained suffix.
 std::set<Id> prefixIds;for(std::size_t i=0;i<begin;++i)prefixIds.insert((*s.eventIndex)[i].id);
 for(const auto&o:s.fillOperations)if(o.id!=record.id){for(const auto&[id,h]:o.members){(void)h;if(!prefixIds.contains(id))return error(ErrorCode::UnsupportedFeature,"fill-withdraw.later-record");}if(!prefixIds.contains(o.boundaryEvent))return error(ErrorCode::UnsupportedFeature,"fill-withdraw.later-record");}
 auto creation=s;creation.revision=first->parentRevision;creation.eventIndex=std::make_shared<const EventList>(s.eventIndex->begin(),first);std::erase_if(creation.fillOperations,[&](const auto&o){return o.id==record.id;});
 valid=validateSnapshot(creation);if(!valid)return valid.error();
 std::array<Id,4> ids{};std::set<Id> used;for(const auto&[id,n]:*s.nodes){(void)n;used.insert(id);}for(const auto&e:*s.eventIndex)used.insert(e.id);
 for(std::size_t i=begin;i<end;++i){const auto&e=(*s.eventIndex)[i];const auto level=e.color.alpha*4;if(level<1||level>4||level!=std::floor(level))return error(ErrorCode::InvalidInput,"fill-withdraw.alpha-class");const auto index=std::size_t(level)-1;if(!ids[index].empty())return error(ErrorCode::InvalidInput,"fill-withdraw.duplicate-alpha");ids[index]=e.id;}
 std::uint64_t counter=1;for(auto&id:ids)if(id.empty()){while(used.contains(Id::fromNumber(counter))){if(counter==UINT64_MAX)return error(ErrorCode::Overflow,"fill-withdraw.ids");++counter;}id=Id::fromNumber(counter);used.insert(id);}
 auto regenerated=closedInkFillCommand(creation,record.boundaryLayer,ids,stop);if(!regenerated)return regenerated.error();
 if(regenerated.value().operations.size()!=record.members.size()+1)return error(ErrorCode::InvalidInput,"fill-withdraw.recipe-mismatch");
 for(std::size_t i=0;i<record.members.size();++i){const auto*paint=std::get_if<Paint>(&regenerated.value().operations[i]);if(!paint||paint->event!=(*s.eventIndex)[begin+i])return error(ErrorCode::InvalidInput,"fill-withdraw.recipe-mismatch");}
 const auto*saved=std::get_if<RecordClosedInkFill>(&regenerated.value().operations.back());if(!saved||saved->operation!=record)return error(ErrorCode::InvalidInput,"fill-withdraw.recipe-mismatch");
 auto candidate=s;auto events=std::make_shared<EventList>();events->reserve(s.eventIndex->size()-record.members.size());for(const auto&e:*s.eventIndex)if(!memberIds.contains(e.id))events->push_back(e);candidate.eventIndex=events;std::erase_if(candidate.fillOperations,[&](const auto&o){return o.id==record.id;});
 valid=validateSnapshot(candidate);if(!valid)return valid.error();if(stop.stop_requested())return error(ErrorCode::Cancelled,"fill-withdraw.cancelled");return candidate;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"fill-withdraw.allocation");}
inline Result<Command> withdrawClosedInkFillCommand(const DocumentSnapshot&s,Id id,std::stop_token stop={}){auto checked=closedInkWithdrawalCandidate(s,id,stop);if(!checked)return checked.error();return Command{WithdrawClosedInkFill{id}};}
}
