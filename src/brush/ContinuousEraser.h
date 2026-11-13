#pragma once
#include "core/Commands.h"
#include <span>
#include <stop_token>
namespace drawborad {
Result<Command> continuousEraserCommand(const DocumentSnapshot&,NodeId,Id,std::span<const InputSample>,double width,double opacity=1,std::stop_token={});
}
