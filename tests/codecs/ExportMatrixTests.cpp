#include "Test.h"
#include "codecs/ExportService.h"
using namespace drawborad;
TEST("C06 CMYK to PNG requires explicit RGB conversion consent"){ExportRequest r;r.format=ExportFormat::Png;r.path="x.png";r.size={10,10};CHECK(!preflightExport(ColorMode::CMYK,r));r.conversionConsent=true;auto p=preflightExport(ColorMode::CMYK,r);CHECK(p);CHECK(p.value().requiresRgbConversion);}
TEST("C06 JPEG alpha requires matte choice"){ExportRequest r;r.format=ExportFormat::Jpeg;r.path="x.jpg";r.size={10,10};r.alpha=true;CHECK(!preflightExport(ColorMode::RGB,r));r.matteChosen=true;CHECK(preflightExport(ColorMode::RGB,r));}
