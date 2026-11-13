#pragma once
#include "core/Commands.h"
#include <span>
#include <stop_token>
namespace drawborad {
Result<Command> chiselInkStrokeCommand(const DocumentSnapshot&,NodeId,Id,std::span<const InputSample>,double width,ColorValue,unsigned angle=0,double opacity=1,std::stop_token={});
}
