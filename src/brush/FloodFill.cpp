#include "FloodFill.h"
#include <algorithm>
#include <cstdlib>
#include <new>
namespace drawborad {
Result<FillMask> floodFill(std::span<const Rgba8> pixels, std::uint32_t width,
                          std::uint32_t height, std::uint32_t seedX,
                          std::uint32_t seedY, std::uint8_t tolerance,
                          FillBudget budget, std::stop_token stop) {
    const auto count=std::uint64_t(width)*height;
    if (!width || !height || count!=pixels.size() || seedX>=width || seedY>=height)
        return error(ErrorCode::InvalidInput, "fill.input");
    if (count>budget.maxPixels || !budget.maxRuns)
        return error(ErrorCode::BudgetExceeded, "fill.budget");
    if (stop.stop_requested()) return error(ErrorCode::Cancelled, "fill.cancelled");
    try {
        std::vector<std::uint8_t> visited(pixels.size(),0);
        const auto seed=pixels[std::size_t(seedY)*width+seedX];
        const auto matches=[&](std::uint32_t x,std::uint32_t y) {
            const auto i=std::size_t(y)*width+x;
            if (visited[i]) return false;
            const auto p=pixels[i];
            if (!p.a && !seed.a) return true;
            return std::abs(int(p.r)-seed.r)<=tolerance &&
                   std::abs(int(p.g)-seed.g)<=tolerance &&
                   std::abs(int(p.b)-seed.b)<=tolerance &&
                   std::abs(int(p.a)-seed.a)<=tolerance;
        };
        FillMask result;
        const auto discover=[&](std::uint32_t x,std::uint32_t y)->Result<FillRun> {
            auto left=x,right=x;
            while (left && matches(left-1,y)) --left;
            while (right+1<width && matches(right+1,y)) ++right;
            if (result.runs.size()>=budget.maxRuns)
                return error(ErrorCode::BudgetExceeded, "fill.runs");
            FillRun run{y,left,right-left+1};
            result.runs.push_back(run);
            result.pixelCount+=run.length;
            std::fill_n(visited.begin()+std::size_t(y)*width+left,run.length,1);
            return run;
        };
        auto first=discover(seedX,seedY);
        if (!first) return first.error();
        for (std::size_t i=0;i<result.runs.size();++i) {
            if (stop.stop_requested()) return error(ErrorCode::Cancelled, "fill.cancelled");
            // Copy because discovering adjacent spans may reallocate runs.
            const auto run=result.runs[i];
            for (int dy : {-1,1}) {
                if ((dy<0 && !run.y) || (dy>0 && run.y+1==height)) continue;
                const auto y=dy<0?run.y-1:run.y+1;
                for (auto x=run.x;x<run.x+run.length;) {
                    if (!matches(x,y)) { ++x; continue; }
                    auto found=discover(x,y);
                    if (!found) return found.error();
                    x=found.value().x+found.value().length;
                }
            }
        }
        std::sort(result.runs.begin(),result.runs.end(),[](const auto& a,const auto& b) {
            return a.y<b.y || (a.y==b.y && a.x<b.x);
        });
        return result;
    } catch (const std::bad_alloc&) {
        return error(ErrorCode::BudgetExceeded, "fill.allocation");
    }
}
}
