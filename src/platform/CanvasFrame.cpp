#include "CanvasFrame.h"
#include <algorithm>
#include <memory>
namespace drawborad {
Result<QImage> renderCanvasFrame(const DocumentSnapshot& snapshot,std::uint64_t count,MergedBudget budget,std::stop_token stop){
    if(!snapshot.eventIndex)return error(ErrorCode::CorruptData,"document.structure");
    auto rendered=renderMergedDocument(snapshot,{snapshot.revision,std::min<std::uint64_t>(count,snapshot.eventIndex->size()),UINT64_MAX},budget,stop);
    if(!rendered)return rendered.error();
    try{
        auto pixels=std::make_unique<TileData>(std::move(rendered.value().merged));
        // The QImage cleanup owns the moved buffer; do not allocate a second full canvas.
        QImage image(pixels->bytes.data(),int(pixels->width),int(pixels->height),
                     qsizetype(pixels->rowStride),QImage::Format_RGBA8888,
                     [](void* p){delete static_cast<TileData*>(p);},pixels.get());
        if(image.isNull())return error(ErrorCode::BudgetExceeded,"canvas.image-allocation");
        pixels.release();return image;
    }catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"canvas.image-allocation");}
}
}
