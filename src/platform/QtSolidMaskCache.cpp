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
bool sameControls(const std::array<QPointF, 6>& a, const std::array<QPointF, 6>& b) {
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].x() != b[i].x() || a[i].y() != b[i].y()) return false;
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
    std::uint64_t pixels{}, imageBytes{};
    if (!multiply(w, h, pixels) || pixels > 20000000ULL ||
        !multiply((w + 3) & ~std::uint64_t(3), h, imageBytes)) return Status::BudgetExceeded;
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
    // Fixed-size metadata: compute unique geometry and reuse before any image allocation.
    std::array<std::size_t, 8> representative{};
    std::array<std::shared_ptr<const MaskRecord>, 8> records{};
    std::size_t uniqueCount = 0, newCount = 0;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        representative[i] = i;
        for (std::size_t j = 0; j < i; ++j) {
            if (sameControls(shapes[i].controls, shapes[j].controls)) {
                representative[i] = representative[j];
                break;
            }
        }
        if (representative[i] != i) continue;
        ++uniqueCount;
        for (const auto& old : entries_) {
            const auto& record = *old.record;
            if (record.width == width && record.height == height && record.runtime == requiredQtRuntime &&
                sameControls(record.controls, shapes[i].controls)) {
                records[i] = old.record;
                break;
            }
        }
        if (!records[i]) ++newCount;
    }
    std::uint64_t newBytes{}, candidateBytes{};
    if (!multiply(imageBytes, newCount, newBytes) || !multiply(imageBytes, uniqueCount, candidateBytes) ||
        stats_.retainedImageBytes > limits_.maxResidentImageBytes ||
        newBytes > limits_.maxResidentImageBytes - stats_.retainedImageBytes)
        return Status::BudgetExceeded;
    preparing_ = true;
    struct Reset { bool& value; ~Reset() { value = false; } } reset{preparing_};
    stats_.peakAdmittedImageBytes = std::max(stats_.peakAdmittedImageBytes,
                                            stats_.retainedImageBytes + newBytes);
    try {
        // Copy input before callbacks. Shared records remain const through preparation.
        std::vector<Entry> candidate;
        candidate.reserve(shapes.size());
        for (const auto& shape : shapes) candidate.push_back({shape, {}});
        QString runtime = requiredQtRuntime;
        std::size_t completed = 0;
        for (std::size_t i = 0; i < candidate.size(); ++i) {
            if (stop.stop_requested()) return Status::Cancelled;
            if (representative[i] != i || records[i]) continue;
            // Only this fresh, unpublished record is ever writable or passed to QPainter.
            auto record = std::make_shared<MaskRecord>();
            record->controls = candidate[i].shape.controls;
            record->width = width;
            record->height = height;
            record->runtime = runtime;
            record->mask = QImage(width, height, QImage::Format_Alpha8);
            if (record->mask.isNull()) return Status::AllocationFailed;
            if (record->mask.sizeInBytes() < 0 ||
                static_cast<std::uint64_t>(record->mask.sizeInBytes()) != imageBytes)
                return Status::UnsupportedBackend;
            record->mask.fill(0);
            QPainter painter(&record->mask);
            if (!painter.isActive()) return Status::UnsupportedBackend;
            painter.setRenderHint(QPainter::Antialiasing);
            const auto path = makePath(record->controls);
            if (stop.stop_requested()) { painter.end(); return Status::Cancelled; }
            ++stats_.rasterCalls;
            // Qt's CPU call cannot be interrupted. Never publish before next stop check.
            painter.fillPath(path, Qt::black);
            if (!painter.end()) return Status::RasterFailed;
            if (progress) progress(progressContext, ++completed);
            if (stop.stop_requested()) return Status::Cancelled;
            records[i] = std::move(record); // mutable owner is consumed; const record only
        }
        for (std::size_t i = 0; i < candidate.size(); ++i)
            candidate[i].record = records[representative[i]];
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
                    it->record->mask.constScanLine(rect.y() + row) + rect.x(), static_cast<std::size_t>(rw));
    return Status::Ok;
}
}
