#pragma once
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QString>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <vector>

namespace drawborad {
// Optional Qt-only adapter. No persistent event or legacy rendering semantics.
// Geometry alpha only: color/opacity/layer compositing remain caller-owned.
// Single-thread owned; callers must not concurrently access this object.
class QtSolidMaskCache final {
public:
    enum class Status { Ok, Reused, Cancelled, UnsupportedBackend, InvalidInput,
                        BudgetExceeded, AllocationFailed, RasterFailed, Busy, MissingEvent, StaleGeneration };
    struct Shape {
        std::uint64_t id{};
        // Closed two-span cubic: A,C1,C2,B,C3,C4; B,C3,C4,A is span two.
        // Authored direction is identity. Qt OddEven includes self-intersections;
        // this adapter does not certify simple topology or reverse invariance.
        std::array<QPointF, 6> controls{};
        bool operator==(const Shape& other) const noexcept {
            if (id != other.id) return false;
            for (std::size_t i = 0; i < controls.size(); ++i)
                if (controls[i].x() != other.controls[i].x() ||
                    controls[i].y() != other.controls[i].y()) return false;
            return true;
        }
    };
    struct Limits {
        std::uint64_t maxResidentImageBytes{64ULL * 1024 * 1024};
        std::size_t maxEvents{8};
    };
    struct Stats {
        std::uint64_t retainedImageBytes{};
        std::uint64_t peakAdmittedImageBytes{}; // old + candidates, NOT total RSS
        std::uint64_t rasterCalls{};
        std::uint64_t publications{};
    };
    // Called after a completed, uninterruptible Qt fillPath, before stop check.
    // May request stop and inspect the old cohort; reentrant prepare returns Busy.
    using Progress = void (*)(void*, std::size_t completed) noexcept;
    QtSolidMaskCache();
    explicit QtSolidMaskCache(Limits limits);
    QtSolidMaskCache(const QtSolidMaskCache&) = delete;
    QtSolidMaskCache& operator=(const QtSolidMaskCache&) = delete;
    QtSolidMaskCache(QtSolidMaskCache&&) = delete;
    QtSolidMaskCache& operator=(QtSolidMaskCache&&) = delete;
    // Exact ordered shape data + dimensions + required runtime identifies cohort.
    // qVersion() must match requiredQtRuntime; no fallback rasterizer is used.
    Status prepare(std::span<const Shape> shapes, int width, int height,
                   const QString& requiredQtRuntime, std::stop_token stop = {},
                   Progress progress = nullptr, void* progressContext = nullptr);
    // Copies alpha bytes only; padding remains untouched. No image handles escape.
    // Rectangle must be in canvas, positive and at most 256 x 256.
    Status readTile(std::uint64_t generation, std::uint64_t id, QRect rect, std::span<std::uint8_t> output,
                    std::size_t outputStride) const;
    // Use only after Ok/Reused; a failed request never supplies a new generation.
    std::uint64_t generation() const noexcept { return stats_.publications; }
    Stats stats() const noexcept { return stats_; }
    std::size_t eventCount() const noexcept { return entries_.size(); }
private:
    struct Entry { Shape shape; QImage mask; };
    Limits limits_;
    Stats stats_;
    std::vector<Entry> entries_;
    int width_{}, height_{};
    QString runtime_;
    bool preparing_{};
};
}
