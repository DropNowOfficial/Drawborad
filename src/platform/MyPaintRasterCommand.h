#pragma once
#include "core/Commands.h"
#include <QString>
#include <stop_token>
namespace drawborad {
struct MyPaintRuntime { QString python, worker, pins; }; // operator-owned paths, never stroke request fields
struct MyPaintGesture {
    std::vector<InputSample> samples;
    ColorValue color;
    double opacity{1}, radiusScale{1};
    QString preset{"pointy_ink"};
    bool mouseSimulated{};
};
struct MyPaintRasterCandidate { Revision baseRevision{}; Command command; Rect bounds; NodeId layer,folder; };
Result<unsigned> desktopMyPaintStrokeCount(const DocumentSnapshot&);
Result<void> validateMyPaintRuntime(const MyPaintRuntime&);
Result<MyPaintRasterCandidate> buildMyPaintRasterCommand(const DocumentSnapshot&,Revision,
    const MyPaintGesture&,NodeId newLayer,NodeId folder,const MyPaintRuntime&,std::stop_token={});
Result<MyPaintRasterCandidate> buildMyPaintPersistentCommand(const DocumentSnapshot&,Revision,
    const MyPaintGesture&,NodeId layer,Id strokeId,const MyPaintRuntime&,std::stop_token={});
}
