#pragma once
#include "core/Commands.h"
#include <stop_token>
namespace drawborad {
Result<Command> cubicInkStrokeCommand(const DocumentSnapshot&,NodeId,Id,const std::array<std::array<double,2>,4>&,double width,ColorValue,double opacity=1,std::stop_token={});
Result<Command> adjustCubicControlsCommand(const DocumentSnapshot&,Id,const std::array<std::array<double,2>,4>&,std::stop_token={});
}
