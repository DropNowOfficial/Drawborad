#include "platform/QtSolidMaskCache.h"
#include <QPainter>
#include <QPainterPath>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
using Cache = drawborad::QtSolidMaskCache;
using Status = Cache::Status;
static Cache::Shape shape(std::uint64_t id, double shift = 0) {
    return {id, {QPointF(20 + shift, 10), QPointF(240, 20), QPointF(240, 220),
                 QPointF(30, 240), QPointF(100, 160), QPointF(5, 70)}};
}
static QImage whole(const Cache::Shape& shape, int w, int h) {
    QImage image(w, h, QImage::Format_Alpha8); image.fill(0);
    QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath p; p.setFillRule(Qt::OddEvenFill); const auto& c = shape.controls;
    p.moveTo(c[0]); p.cubicTo(c[1], c[2], c[3]); p.cubicTo(c[4], c[5], c[0]); p.closeSubpath();
    painter.fillPath(p, Qt::black); assert(painter.end()); return image;
}
static std::vector<std::uint8_t> tile(const Cache& c, std::uint64_t id) {
    std::vector<std::uint8_t> result(256 * 256);
    assert(c.readTile(c.generation(), id, {0,0,256,256}, result, 256) == Status::Ok);
    return result;
}
struct CancelContext { Cache* cache; std::stop_source* stop; std::span<const Cache::Shape> input; QString runtime; std::uint64_t generation; bool seen{}; };
static void cancelAfterOne(void* opaque, std::size_t count) noexcept {
    auto& context = *static_cast<CancelContext*>(opaque);
    assert(count == 1); context.seen = true;
    assert(context.cache->generation() == context.generation);
    assert(context.cache->prepare(context.input, 257, 259, context.runtime) == Status::Busy);
    context.stop->request_stop();
}
int main() {
    const QString runtime = QString::fromLatin1(qVersion());
    Cache cache; std::vector<Cache::Shape> shapes{shape(1), shape(2,3), shape(3,-4)};
    const auto original = shapes;
    assert(cache.prepare(shapes,257,259,runtime) == Status::Ok);
    assert(shapes == original && cache.eventCount() == 3);
    const auto bytes = 260ULL * 259 * 3;
    assert(cache.stats().retainedImageBytes == bytes && cache.stats().peakAdmittedImageBytes == bytes);
    assert(cache.stats().rasterCalls == 3 && cache.stats().publications == 1);
    const auto generation = cache.generation();
    // Alternate overlapping events/tiles repeatedly. Tile reads cannot rasterize.
    for (int repeat=0; repeat<3; ++repeat) for (const auto& s : shapes) {
        const auto reference = whole(s,257,259);
        for (int y=0;y<259;y+=71) for (int x=0;x<257;x+=67) {
            const int w=std::min(67,257-x), h=std::min(71,259-y); const std::size_t stride=w+5;
            std::vector<std::uint8_t> buffer(stride*h, 231);
            assert(cache.readTile(generation,s.id,{x,y,w,h},buffer,stride)==Status::Ok);
            for(int row=0;row<h;++row) {
                for(int col=0;col<w;++col) assert(buffer[row*stride+col]==reference.constScanLine(y+row)[x+col]);
                for(std::size_t col=w;col<stride;++col) assert(buffer[row*stride+col]==231);
            }
        }
    }
    assert(cache.stats().rasterCalls==3);
    assert(cache.prepare(shapes,257,259,runtime)==Status::Reused && cache.generation()==generation);
    const auto prior=tile(cache,1);
    auto bad=shapes; bad[0].controls[1].setX(std::numeric_limits<double>::quiet_NaN());
    assert(cache.prepare(bad,257,259,runtime)==Status::InvalidInput);
    bad=shapes; bad[1].id=bad[0].id; assert(cache.prepare(bad,257,259,runtime)==Status::InvalidInput);
    bad=shapes; bad[0].controls[0].setX(1000001); assert(cache.prepare(bad,257,259,runtime)==Status::InvalidInput);
    bad=shapes; bad[0].id=0; assert(cache.prepare(bad,257,259,runtime)==Status::InvalidInput);
    assert(cache.prepare(shapes,8193,1,runtime)==Status::InvalidInput);
    assert(cache.prepare(shapes,-1,259,runtime)==Status::InvalidInput);
    assert(cache.prepare(shapes,4001,5000,runtime)==Status::BudgetExceeded);
    assert(cache.prepare(shapes,257,259,QStringLiteral("not-this-runtime"))==Status::UnsupportedBackend);
    std::vector<Cache::Shape> many; for(unsigned i=1;i<=9;++i) many.push_back(shape(i));
    assert(cache.prepare(many,257,259,runtime)==Status::BudgetExceeded);
    std::stop_source pre; pre.request_stop();
    assert(cache.prepare(shapes,257,259,runtime,pre.get_token())==Status::Cancelled);
    assert(cache.stats().rasterCalls==3 && tile(cache,1)==prior && cache.generation()==generation);
    auto changed=shapes; changed[0].controls[1]+=QPointF(20,5);
    std::stop_source during; CancelContext context{&cache,&during,changed,runtime,generation};
    assert(cache.prepare(changed,257,259,runtime,during.get_token(),cancelAfterOne,&context)==Status::Cancelled);
    assert(context.seen && cache.stats().rasterCalls==4 && tile(cache,1)==prior);
    assert(cache.stats().peakAdmittedImageBytes==2*bytes && cache.stats().retainedImageBytes==bytes);
    assert(cache.prepare(changed,257,259,runtime)==Status::Ok);
    assert(cache.generation()==generation+1 && tile(cache,1)!=prior);
    std::vector<std::uint8_t> output(256*256,199); const auto untouched=output;
    assert(cache.readTile(generation,1,{0,0,1,1},output,1)==Status::StaleGeneration);
    assert(cache.readTile(cache.generation(),99,{0,0,1,1},output,1)==Status::MissingEvent);
    assert(cache.readTile(cache.generation(),1,{-1,0,1,1},output,1)==Status::InvalidInput);
    assert(cache.readTile(cache.generation(),1,{0,0,257,1},output,257)==Status::InvalidInput);
    assert(cache.readTile(cache.generation(),1,{256,258,2,2},output,2)==Status::InvalidInput);
    assert(cache.readTile(cache.generation(),1,{0,0,2,2},output,1)==Status::InvalidInput);
    assert(cache.readTile(cache.generation(),1,{0,0,2,2},output,std::numeric_limits<std::size_t>::max())==Status::InvalidInput);
    assert(cache.readTile(cache.generation(),1,{0,0,2,2},std::span<std::uint8_t>(output).first(3),2)==Status::InvalidInput);
    assert(output==untouched);
    Cache limited({bytes,8}); assert(limited.prepare(shapes,257,259,runtime)==Status::Ok);
    assert(limited.prepare(shapes,257,259,runtime)==Status::Reused);
    assert(limited.prepare(changed,257,259,runtime)==Status::BudgetExceeded);
    assert(limited.stats().rasterCalls==3 && limited.stats().retainedImageBytes==bytes);
    assert(limited.prepare({},257,259,runtime)==Status::Ok && limited.eventCount()==0);
    assert(limited.stats().retainedImageBytes==0);
    assert(limited.readTile(limited.generation(),1,{0,0,1,1},output,1)==Status::MissingEvent);
    Cache unsupportedLimits({std::numeric_limits<std::uint64_t>::max(),8});
    assert(unsupportedLimits.prepare(shapes,257,259,runtime)==Status::InvalidInput);
    assert(cache.stats().rasterCalls==7); // no rendering in reads/failures/cache hit
    Cache keys; assert(keys.prepare(shapes,257,259,runtime)==Status::Ok);
    auto tiny=shapes; tiny[0].controls[1].setX(std::nextafter(tiny[0].controls[1].x(), INFINITY));
    assert(keys.prepare(tiny,257,259,runtime)==Status::Ok && keys.stats().rasterCalls==6);
    std::swap(tiny[0],tiny[2]);
    assert(keys.prepare(tiny,257,259,runtime)==Status::Ok && keys.stats().rasterCalls==9);
    auto duplicates=shapes; duplicates[1].controls=duplicates[0].controls;
    assert(keys.prepare(duplicates,257,259,runtime)==Status::Ok);
    assert(keys.stats().retainedImageBytes==bytes); // same geometry, different ids still accounted
    const auto keyGen=keys.generation(); const auto keyCalls=keys.stats().rasterCalls;
    bad=shapes; bad[0].controls[0].setY(INFINITY);
    assert(keys.prepare(bad,257,259,runtime)==Status::InvalidInput);
    assert(keys.prepare(shapes,0,259,runtime)==Status::InvalidInput);
    assert(keys.prepare(shapes,std::numeric_limits<int>::max(),259,runtime)==Status::InvalidInput);
    assert(keys.generation()==keyGen && keys.stats().rasterCalls==keyCalls);
    assert(keys.readTile(keyGen+1,1,{0,0,1,1},output,1)==Status::StaleGeneration);
    assert(output==untouched);
    for (const auto [width,height,count] : {std::array<int,3>{1725,2625,3}, std::array<int,3>{3300,5100,2}}) {
        Cache native;
        std::vector<Cache::Shape> inputs;
        for(int i=0;i<count;++i) {
            auto value=shape(static_cast<std::uint64_t>(i+1),i*2);
            for(auto& p:value.controls) p=QPointF(p.x()*width/257.0,p.y()*height/259.0);
            inputs.push_back(value);
        }
        const auto start=std::chrono::steady_clock::now();
        assert(native.prepare(inputs,width,height,runtime)==Status::Ok);
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        const std::uint64_t total=((std::uint64_t(width)+3)&~std::uint64_t(3))*height*count;
        assert(native.stats().retainedImageBytes==total && native.stats().rasterCalls==static_cast<unsigned>(count));
        const auto oldGeneration=native.generation();
        std::vector<std::uint8_t> nativeBefore(128*128);
        assert(native.readTile(oldGeneration,1,{width/2,height/2,128,128},nativeBefore,128)==Status::Ok);
        // Whole Alpha8 reference comparison in an interior/boundary-spanning native crop.
        const auto reference=whole(inputs.front(),width,height);
        for(int y=0;y<128;++y)for(int x=0;x<128;++x)
            assert(nativeBefore[y*128+x]==reference.constScanLine(height/2+y)[width/2+x]);
        inputs.front().controls[1]+=QPointF(7,11);
        if(count==2) {
            assert(total==33660000 && 2*total>64ULL*1024*1024);
            assert(native.prepare(inputs,width,height,runtime)==Status::BudgetExceeded);
            assert(native.generation()==oldGeneration && native.stats().rasterCalls==2);
            std::vector<std::uint8_t> nativeAfter(128*128);
            assert(native.readTile(oldGeneration,1,{width/2,height/2,128,128},nativeAfter,128)==Status::Ok);
            assert(nativeAfter==nativeBefore);
        } else {
            assert(total==13608000);
            assert(native.prepare(inputs,width,height,runtime)==Status::Ok);
            assert(native.stats().peakAdmittedImageBytes==2*total);
        }
        std::cout<<"native "<<width<<"x"<<height<<" events="<<count<<" initial_ms="<<elapsed
                 <<" retained_image_bytes="<<total<<" replacement="<<(count==2?"budget-refused-old-preserved":"accepted")<<"\n";
    }
    std::cout<<"PASS Qt wholemask cohort: three overlapping events, exact crops, no tile reraster; atomic cancel and old+candidate budget; generation and caller-buffer guards; no format integration\n";
}
