#include "render/GraphicsResources.h"
namespace drawborad {
Result<void> recoverGraphicsDevice(GraphicsResources& g,DisplayCache& d,bool canRecreate,const DocumentSnapshot& s){auto v=validateSnapshot(s);if(!v)return v.error();d.clear();g.generation++;g.available=canRecreate;if(!canRecreate)return error(ErrorCode::DeviceLost,"graphics.cpu-fallback");return{};}
}
