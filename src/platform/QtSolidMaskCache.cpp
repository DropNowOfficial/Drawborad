#include "QtSolidMaskCache.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace drawborad {
namespace {
bool multiply(std::uint64_t a, std::uint64_t b, std::uint64_t& result) {
    if (b && a > std::numeric_limits<std::uint64_t>::max() / b) return false;
    result = a * b;
    return true;
}
QPainterPath makePath(const std::array<QPointF, 6>& p) {
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    path.moveTo(p[0]);
    path.cubicTo(p[1], p[2], p[3]);
    path.cubicTo(p[4], p[5], p[0]);
    path.closeSubpath();
    return path;
}
}
QtSolidMaskCache::QtSolidMaskCache() : QtSolidMaskCache(Limits{}) {}
QtSolidMaskCache::QtSolidMaskCache(Limits limits) : limits_(limits) {}
QtSolidMaskCache::Status QtSolidMaskCache::prepare(
    std::span<const Shape> shapes, int width, int height, const QString& requiredQtRuntime,
    std::stop_token stop, Progress progress, void* progressContext) {
    if (preparing_) return Status::Busy;
    if (stop.stop_requested()) return Status::Cancelled;
    if (requiredQtRuntime != QString::fromLatin1(qVersion())) return Status::UnsupportedBackend;
    // Fixed implementation ceilings cannot be raised by supplying permissive Limits.
    if (limits_.maxEvents > 8 || !limits_.maxEvents ||
        !limits_.maxResidentImageBytes || limits_.maxResidentImageBytes > 64ULL * 1024 * 1024)
        return Status::InvalidInput;
    if (shapes.size() > limits_.maxEvents) return Status::BudgetExceeded;
    if (width < 1 || height < 1 || width > 8192 || height > 8192) return Status::InvalidInput;
    const auto w = static_cast<std::uint64_t>(width);
    const auto h = static_cast<std::uint64_t>(height);
    std::uint64_t pixels{}, imageBytes{}, candidateBytes{};
    if (!multiply(w, h, pixels) || pixels > 20000000ULL ||
        !multiply((w + 3) & ~std::uint64_t(3), h, imageBytes) ||
        !multiply(imageBytes, shapes.size(), candidateBytes)) return Status::BudgetExceeded;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        if (stop.stop_requested()) return Status::Cancelled;
        if (!shapes[i].id) return Status::InvalidInput;
        for (std::size_t j = 0; j < i; ++j)
            if (shapes[j].id == shapes[i].id) return Status::InvalidInput;
        for (const auto& point : shapes[i].controls)
            if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
                std::abs(point.x()) > 1000000 || std::abs(point.y()) > 1000000)
                return Status::InvalidInput;
    }
    const bool identical = width == width_ && height == height_ && runtime_ == requiredQtRuntime &&
        shapes.size() == entries_.size() && std::equal(shapes.begin(), shapes.end(), entries_.begin(),
            [](const Shape& a, const Entry& b) { return a == b.shape; });
    if (identical) return Status::Reused;
    if (stats_.retainedImageBytes > limits_.maxResidentImageBytes ||
        candidateBytes > limits_.maxResidentImageBytes - stats_.retainedImageBytes)
        return Status::BudgetExceeded;
    preparing_ = true;
    struct Reset { bool& value; ~Reset() { value = false; } } reset{preparing_};
    stats_.peakAdmittedImageBytes = std::max(stats_.peakAdmittedImageBytes,
                                            stats_.retainedImageBytes + candidateBytes);
    try {
        // Copy all authored input before callbacks. No QImage aliases are exported.
        std::vector<Entry> candidate;
        candidate.reserve(shapes.size());
        for (const auto& shape : shapes) candidate.push_back({shape, {}});
        QString runtime = requiredQtRuntime;
        for (std::size_t i = 0; i < candidate.size(); ++i) {
            if (stop.stop_requested()) return Status::Cancelled;
            auto& entry = candidate[i];
            entry.mask = QImage(width, height, QImage::Format_Alpha8);
            if (entry.mask.isNull()) return Status::AllocationFailed;
            if (entry.mask.sizeInBytes() < 0 ||
                static_cast<std::uint64_t>(entry.mask.sizeInBytes()) != imageBytes)
                return Status::UnsupportedBackend;
            entry.mask.fill(0);
            QPainter painter(&entry.mask);
            if (!painter.isActive()) return Status::UnsupportedBackend;
            painter.setRenderHint(QPainter::Antialiasing);
            const auto path = makePath(entry.shape.controls);
            if (stop.stop_requested()) { painter.end(); return Status::Cancelled; }
            ++stats_.rasterCalls;
            // Qt's CPU call cannot be interrupted. Never publish before next stop check.
            painter.fillPath(path, Qt::black);
            if (!painter.end()) return Status::RasterFailed;
            if (progress) progress(progressContext, i + 1);
            if (stop.stop_requested()) return Status::Cancelled;
        }
        if (stop.stop_requested()) return Status::Cancelled;
        entries_.swap(candidate);
        runtime_.swap(runtime);
        width_ = width;
        height_ = height;
        stats_.retainedImageBytes = candidateBytes;
        ++stats_.publications;
        return Status::Ok;
    } catch (const std::bad_alloc&) {
        return Status::AllocationFailed;
    }
}
QtSolidMaskCache::Status QtSolidMaskCache::readTile(
    std::uint64_t generation, std::uint64_t id, QRect rect, std::span<std::uint8_t> output, std::size_t stride) const {
    if (!generation || generation != stats_.publications) return Status::StaleGeneration;
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [id](const Entry& e) { return e.shape.id == id; });
    if (it == entries_.end()) return Status::MissingEvent;
    const auto rw = rect.width(), rh = rect.height();
    if (rw < 1 || rh < 1 || rw > 256 || rh > 256 || rect.x() < 0 || rect.y() < 0 ||
        std::int64_t(rect.x()) + rw > width_ || std::int64_t(rect.y()) + rh > height_ ||
        stride < static_cast<std::size_t>(rw)) return Status::InvalidInput;
    std::uint64_t tail{};
    if (!multiply(static_cast<std::uint64_t>(rh - 1), stride, tail) ||
        tail > std::numeric_limits<std::size_t>::max() - static_cast<std::size_t>(rw) ||
        output.size() < tail + static_cast<std::size_t>(rw)) return Status::InvalidInput;
    for (int row = 0; row < rh; ++row)
        std::memcpy(output.data() + static_cast<std::size_t>(row) * stride,
                    it->mask.constScanLine(rect.y() + row) + rect.x(), static_cast<std::size_t>(rw));
    return Status::Ok;
}
}
