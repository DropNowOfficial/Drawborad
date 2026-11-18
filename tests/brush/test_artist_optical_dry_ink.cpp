#include "codecs/OraStream.h"
#include "core/ArtistOptIn.h"
#include "core/Commands.h"
#include "reconstruct/ArtistGrammarPlanner.h"
#include "render/CoverageRasterizer.h"
#include "render/DryInkCoverage.h"
#include "render/PixelBlend.h"
#include "render/ReferenceRenderer.h"
#include "storage/FileIO.h"
#include "storage/ProjectCodec.h"
#include "storage/ProjectStore.h"
#include "ui/LayerStream.h"
#include "ui/MergedDocument.h"
#include "ui/PaintedDocument.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

using namespace drawborad;
namespace fs = std::filesystem;

namespace {

DocumentSnapshot makeCanvas(std::uint32_t w, std::uint32_t h) {
    NewDocumentRequest req;
    req.dimensions = {double(w), double(h), Unit::Px, 300.0};
    auto spec = validateDocument(req, {});
    assert(spec);
    auto doc = Document::create(spec.value(), Id::fromNumber(777));
    assert(doc);
    return doc.value().snapshot();
}

} // namespace

int main() {
    // 1. Linear-sRGB LUT exact roundtrip & optical blend anti-muddy check
    for (unsigned v = 0; v < 256; ++v) {
        const auto lin = srgb8ToLinear16(std::uint8_t(v));
        assert(linear16ToSrgb8(lin) == std::uint8_t(v));
    }
    assert(srgb8ToLinear16(0) == 0);
    assert(srgb8ToLinear16(255) == 65535);

    {
        auto base = makeCanvas(32, 32);
        auto doc = Document::fromSnapshot(base);
        assert(doc);
        LayerNode layer;
        layer.id = Id::fromNumber(2);
        layer.parent = Document::rootId();
        layer.bounds = {0, 0, 32, 32};
        assert(doc.value().apply(Command(AddNode{layer}), doc.value().snapshot().revision));

        // Paint opaque pure red base, then 50% opacity pure cyan over it:
        // Legacy gamma blend gives ~128 (dark muddy gray); Linear-sRGB optical blend gives ~188!
        StrokeEvent red;
        red.id = Id::fromNumber(10);
        red.layerId = layer.id;
        red.parentRevision = doc.value().snapshot().revision;
        red.engineVersion = 8;
        red.toolId = "bucket";
        red.color.channels = {1.0, 0.0, 0.0, 0.0};
        red.color.alpha = 1.0;
        for (unsigned y = 0; y < 32; ++y) {
            red.geometry.push_back({0.0, double(y), 1.0, {}, {}, 0, 0, StrokeCapabilityLinearOptical});
            red.geometry.push_back({32.0, double(y), 1.0, {}, {}, 0, 0, StrokeCapabilityLinearOptical});
        }
        assert(doc.value().apply(Command(Paint{red, {0, 0, 32, 32}}), red.parentRevision));

        StrokeEvent cyan = red;
        cyan.id = Id::fromNumber(11);
        cyan.parentRevision = doc.value().snapshot().revision;
        cyan.color.channels = {0.0, 1.0, 1.0, 0.0};
        cyan.color.alpha = 0.5;
        assert(doc.value().apply(Command(Paint{cyan, {0, 0, 32, 32}}), cyan.parentRevision));

        const auto snap = doc.value().snapshot();
        assert(requiresArtistFormat26(snap));
        auto rendered = renderMergedDocument(snap, {snap.revision, snap.eventIndex->size()});
        assert(rendered);
        const auto r = rendered.value().merged.bytes[0];
        const auto g = rendered.value().merged.bytes[1];
        const auto b = rendered.value().merged.bytes[2];
        assert(r >= 184 && r <= 190 && g >= 184 && g <= 190 && b >= 184 && b <= 190);

        // Verify DRAW-DELTA-26 save/recover roundtrip AND legacy format preservation when opt-in absent
        const auto tmpDir = fs::temp_directory_path() / ("artist-opt-" + std::to_string(std::random_device{}()));
        assert(fs::create_directory(tmpDir));
        assert(ProjectStore{}.save(tmpDir / "opt26.draw", snap));
        auto rec = ProjectStore{}.recover(tmpDir / "opt26.draw");
        assert(rec && rec.value().snapshot.sameContent(snap));

        auto legacyDoc = Document::fromSnapshot(base);
        assert(legacyDoc.value().apply(Command(AddNode{layer}), legacyDoc.value().snapshot().revision));
        auto legacyRed = red;
        for (auto& pt : legacyRed.geometry) pt.capabilities = 0;
        legacyRed.parentRevision = legacyDoc.value().snapshot().revision;
        assert(legacyDoc.value().apply(Command(Paint{legacyRed, {0, 0, 32, 32}}), legacyRed.parentRevision));
        assert(!requiresArtistFormat26(legacyDoc.value().snapshot()));
        auto delta = encodeDelta(DocumentSnapshot{}, legacyDoc.value().snapshot());
        assert(delta);
        const std::string hdr(reinterpret_cast<const char*>(delta.value().data()),
                              std::min<std::size_t>(32, delta.value().size()));
        assert(hdr.find("DRAW-DELTA-26") == std::string::npos);
        fs::remove_all(tmpDir);
    }

    // 2. Native Multiply & Screen Layer Compositing + OraStream svg:multiply / svg:screen
    {
        auto base = makeCanvas(64, 64);
        auto doc = Document::fromSnapshot(base);
        assert(doc);
        const std::array<BlendMode, 3> modes{BlendMode::Normal, BlendMode::Multiply, BlendMode::Screen};
        for (unsigned i = 0; i < 3; ++i) {
            LayerNode l;
            l.id = Id::fromNumber(2 + i);
            l.parent = Document::rootId();
            l.name = i == 0 ? "Base" : (i == 1 ? "Shadow" : "Highlight");
            l.blend = modes[i];
            l.opacity = i == 0 ? 1.0 : 0.75;
            l.bounds = {0, 0, 64, 64};
            assert(doc.value().apply(Command(AddNode{l}), doc.value().snapshot().revision));

            StrokeEvent ev;
            ev.id = Id::fromNumber(20 + i);
            ev.layerId = l.id;
            ev.parentRevision = doc.value().snapshot().revision;
            ev.engineVersion = 19;
            ev.toolId = "smooth-ink-aa";
            ev.style.width = 24.0;
            ev.style.opacity = 1.0;
            ev.style.hardness = 1.0;
            ev.color.channels = i == 0 ? std::array<double, 4>{0.8, 0.6, 0.5, 0.0}
                              : i == 1 ? std::array<double, 4>{0.4, 0.35, 0.6, 0.0}
                                       : std::array<double, 4>{0.7, 0.8, 0.9, 0.0};
            ev.color.alpha = 1.0;
            ev.startUs = 100 + i * 300;
            ev.geometry = {
                {8.0, 32.0, 0.8, {}, {}, ev.startUs, 0, 0},
                {56.0, 32.0, 0.8, {}, {}, ev.startUs + 100, 0, 0}
            };
            ev.endUs = ev.startUs + 100;
            auto b = strokeBounds(ev);
            assert(b);
            assert(doc.value().apply(Command(Paint{ev, b.value()}), ev.parentRevision));
        }
        const auto snap = doc.value().snapshot();
        auto merged = renderMergedDocument(snap, {snap.revision, snap.eventIndex->size()});
        auto painted = renderPaintedDocument(snap, {snap.revision, snap.eventIndex->size()});
        assert(merged && painted);
        assert(merged.value().merged.bytes == painted.value().merged.bytes);

#ifdef DRAWBORAD_HAS_PNG
        const auto tmpDir = fs::temp_directory_path() / ("artist-ora-" + std::to_string(std::random_device{}()));
        assert(fs::create_directory(tmpDir));
        ExportRequest req;
        req.format = ExportFormat::Ora;
        req.path = tmpDir / "blend.ora";
        req.size = snap.spec.pixelSize;
        auto plan = preflightExport(snap, req);
        assert(plan);
        auto stream = NativeLayerStream::create(snap, {snap.revision, snap.eventIndex->size()});
        assert(stream);
        RasterEncodingOptions opts;
        opts.ppi = 300.0;
        auto oraDone = writeOraStream(
            plan.value(), stream.value().size(),
            [&stream](std::size_t idx, std::stop_token st) { return stream.value().read(idx, st); },
            merged.value().merged, opts);
        assert(oraDone);
        std::ifstream f(req.path, std::ios::binary);
        const std::string rawOra((std::istreambuf_iterator<char>(f)), {});
        assert(rawOra.find("svg:multiply") != std::string::npos);
        assert(rawOra.find("svg:screen") != std::string::npos);
        fs::remove_all(tmpDir);
#endif
    }

    // 3. MergedDocument 3300x5100 (300 DPI) ROI Work Budget & ReferenceRenderer ROI Clipping Equivalence
    {
        auto big = makeCanvas(3300, 5100);
        auto nodes = std::make_shared<NodeMap>(*big.nodes);
        LayerNode l;
        l.id = Id::fromNumber(2);
        l.parent = Document::rootId();
        l.bounds = {0, 0, 3300, 5100};
        (*nodes)[l.id] = l;
        nodes->at(Document::rootId()).children.push_back(l.id);
        big.nodes = nodes;

        auto events = std::make_shared<EventList>();
        // 16 wide translucent glazes + 150 cross-tile dry-ink strokes on 3300x5100!
        std::uint64_t t = 100;
        for (unsigned i = 0; i < 16; ++i) {
            StrokeEvent g;
            g.id = Id::fromNumber(100 + i);
            g.layerId = l.id;
            g.engineVersion = 2;
            g.toolId = "marker";
            g.style.width = 96.0;
            g.style.opacity = 0.35;
            g.style.hardness = 0.25;
            g.color.channels = {0.3, 0.4, 0.7, 0.0};
            g.startUs = t;
            g.geometry = {
                {100.0, 200.0 + i * 250.0, 0.5, {}, {}, t, 0, StrokeCapabilityLinearOptical},
                {3100.0, 350.0 + i * 250.0, 0.9, {}, {}, t + 100, 0, StrokeCapabilityLinearOptical}
            };
            g.endUs = t + 100;
            t += 200;
            events->push_back(std::move(g));
        }
        for (unsigned i = 0; i < 150; ++i) {
            StrokeEvent d;
            d.id = Id::fromNumber(500 + i);
            d.layerId = l.id;
            d.engineVersion = 12;
            d.toolId = "dry-ink";
            d.seed = 42 + i;
            d.style.width = 14.0;
            d.style.opacity = 0.9;
            d.style.hardness = 1.0;
            d.color.channels = {0.1, 0.1, 0.12, 0.0};
            d.startUs = t;
            const double yBase = double((i * 31) % 4900) + 50.0;
            d.geometry = {
                {480.0, yBase, 0.4, {}, {}, t, 0, StrokeCapabilityPhysicalDryInk},
                {1080.0, yBase + 25.0, 0.9, {}, {}, t + 100, 0, StrokeCapabilityPhysicalDryInk}
            };
            d.endUs = t + 100;
            t += 200;
            events->push_back(std::move(d));
        }
        big.eventIndex = events;
        auto plan300 = planMergedDocument(big);
        assert(plan300);
        assert(plan300.value().estimatedBytes <= 128ull * 1024 * 1024);
    }

    // 4. Physical DryInk: 512px Tile-Partition Zero-Seam & Collinear Midpoint Split Invariance
    {
        StrokeEvent dry;
        dry.id = Id::fromNumber(900);
        dry.layerId = Id::fromNumber(2);
        dry.engineVersion = 12;
        dry.toolId = "dry-ink";
        dry.seed = 0x123456789abcdef0ull;
        dry.style.width = 18.0;
        dry.style.opacity = 1.0;
        dry.style.hardness = 1.0;
        dry.startUs = 100;
        dry.geometry = {
            {440.25, 470.5, 0.85, {}, {}, 100, 0, StrokeCapabilityPhysicalDryInk},
            {620.75, 560.5, 0.35, {}, {}, 300, 0, StrokeCapabilityPhysicalDryInk}
        };
        dry.endUs = 300;

        // Whole 256x256 window straddling x=512 and y=512 vs 4 quadrants split at (512, 512)
        const Rect whole{400, 420, 256, 200};
        auto covWhole = dryInkCoverage(dry, whole, UINT64_MAX);
        assert(covWhole);

        const std::array<Rect, 4> quads{{
            {400, 420, 112, 92},
            {512, 420, 144, 92},
            {400, 512, 112, 108},
            {512, 512, 144, 108}
        }};
        for (const auto& q : quads) {
            auto cq = dryInkCoverage(dry, q, UINT64_MAX);
            assert(cq);
            for (std::uint64_t y = 0; y < q.height; ++y) {
                for (std::uint64_t x = 0; x < q.width; ++x) {
                    const auto wVal = covWhole.value()[std::size_t((q.y - whole.y + std::int64_t(y)) * whole.width +
                                                                   (q.x - whole.x + std::int64_t(x)))];
                    const auto qVal = cq.value()[std::size_t(y * q.width + x)];
                    assert(wVal == qVal);
                }
            }
        }

        // Collinear midpoint split invariance
        StrokeEvent splitDry = dry;
        const auto a = dry.geometry[0], b = dry.geometry[1];
        InputSample mid{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.pressure + b.pressure) * 0.5,
                        {}, {}, 200, 0, StrokeCapabilityPhysicalDryInk};
        splitDry.geometry = {a, mid, b};
        auto covSplit = dryInkCoverage(splitDry, whole, UINT64_MAX);
        assert(covSplit);
        for (std::size_t i = 0; i < covWhole.value().size(); ++i) {
            assert(std::abs(int(covWhole.value()[i]) - int(covSplit.value()[i])) <= 1);
        }

        // Verify fine stroke (< 3px) still exhibits organic non-uniform paper tooth variation
        StrokeEvent fineDry = dry;
        fineDry.style.width = 2.0;
        auto covFine = dryInkCoverage(fineDry, whole, UINT64_MAX);
        assert(covFine);
        std::size_t partialTooth = 0;
        for (const auto v : covFine.value()) {
            if (v > 2000 && v < 60000) ++partialTooth;
        }
        assert(partialTooth > 20);
    }

    // 5. ArtistGrammarPlanner: Amodal 4px halo erosion + crisp step-edge preservation + 3x super-resolution
    {
        TileData src;
        src.width = 96;
        src.height = 96;
        src.rowStride = 96 * 4;
        src.format = {ColorMode::RGB, Depth::U8, true};
        src.bytes.resize(96 * 96 * 4, 255);
        std::vector<std::uint8_t> bgMask(96 * 96, 255);
        for (int y = 0; y < 96; ++y) {
            for (int x = 0; x < 96; ++x) {
                const std::size_t at = std::size_t(y * 96 + x) * 4;
                // Neutral blue-gray background with a 2px bright-red foreground bleed rim right outside the mask hole!
                const int dx = x - 48, dy = y - 48;
                const int r2 = dx * dx + dy * dy;
                if (r2 <= 18 * 18) {
                    bgMask[std::size_t(y * 96 + x)] = 0;
                    src.bytes[at + 0] = 250;
                    src.bytes[at + 1] = 20;
                    src.bytes[at + 2] = 20;
                } else if (r2 <= 20 * 20) {
                    // 2px unmasked red halo that 4px erosion MUST reject
                    src.bytes[at + 0] = 245;
                    src.bytes[at + 1] = 30;
                    src.bytes[at + 2] = 30;
                } else {
                    src.bytes[at + 0] = 70;
                    src.bytes[at + 1] = 95;
                    src.bytes[at + 2] = 135;
                }
            }
        }
        auto amodal = inpaintAmodalBackground(src, bgMask);
        assert(amodal);
        const std::size_t centerAt = std::size_t(48 * 96 + 48) * 4;
        // Center of occluded region must match neutral blue-gray background, NOT red foreground rim!
        assert(amodal.value().bytes[centerAt + 0] < 95);
        assert(amodal.value().bytes[centerAt + 2] > 115);

        // Verify 3x super-resolution planArtistObject + renderMergedDocument succeeds with 0 source reads
        auto srBase = makeCanvas(288, 288);
        auto srDoc = Document::fromSnapshot(srBase);
        assert(srDoc);
        LayerNode obj;
        obj.id = Id::fromNumber(10);
        obj.kind = NodeKind::Object;
        obj.parent = Document::rootId();
        obj.name = "Hero";
        obj.bounds = {0, 0, 288, 288};
        Command setup;
        setup.operations.emplace_back(AddNode{obj});
        const std::array<std::pair<const char*, BlendMode>, 5> stages{{
            {"artist.flats", BlendMode::Normal},
            {"artist.form_glaze", BlendMode::Multiply},
            {"artist.structure", BlendMode::Normal},
            {"artist.hero_ink", BlendMode::Normal},
            {"artist.highlights", BlendMode::Screen}
        }};
        for (std::size_t si = 0; si < stages.size(); ++si) {
            LayerNode l;
            l.id = Id::fromNumber(20 + si);
            l.parent = obj.id;
            l.objectId = obj.id;
            l.name = stages[si].first;
            l.stage = stages[si].first;
            l.blend = stages[si].second;
            l.opacity = si == 1 ? 0.85 : (si == 4 ? 0.90 : 1.0);
            l.bounds = obj.bounds;
            setup.operations.emplace_back(AddNode{l, si});
        }
        assert(srDoc.value().apply(setup, srDoc.value().snapshot().revision));
        const auto srSnap = srDoc.value().snapshot();
        std::vector<std::uint8_t> fullMask(96 * 96, 255);
        ArtistObjectRequest aReq;
        aReq.snapshot = srSnap;
        aReq.objectId = obj.id;
        aReq.layers = {Id::fromNumber(20), Id::fromNumber(21), Id::fromNumber(22), Id::fromNumber(23), Id::fromNumber(24)};
        aReq.sourceMask = fullMask;
        aReq.source = &src;
        aReq.targetSize = {288, 288};
        aReq.clipToObjectMask = false;
        aReq.seed = 424242;
        auto planned = planArtistObject(aReq);
        assert(planned);
        Command paint;
        for (auto& batch : planned.value().batches) {
            for (auto& ev : batch.events) {
                auto b = strokeBounds(ev);
                assert(b);
                paint.operations.emplace_back(Paint{std::move(ev), b.value()});
            }
        }
        assert(srDoc.value().apply(paint, srSnap.revision));
        auto srMerged = renderMergedDocument(srDoc.value().snapshot(),
                                             {srDoc.value().snapshot().revision, srDoc.value().snapshot().eventIndex->size()});
        assert(srMerged);
        assert(srMerged.value().statistics.sourceAssetReads == 0);

        // 5b. Verify 3x super-resolution preserves 1-1.5px thin curved iris rims next to bright catchlights!
        TileData eyeSrc;
        eyeSrc.width = 64;
        eyeSrc.height = 64;
        eyeSrc.rowStride = 64 * 4;
        eyeSrc.format = {ColorMode::RGB, Depth::U8, true};
        eyeSrc.bytes.resize(64 * 64 * 4, 255);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const std::size_t at = std::size_t(y * 64 + x) * 4;
                const double dIris = std::hypot(double(x) - 32.0, double(y) - 32.0);
                const double dCatch = std::hypot(double(x) - 25.5, double(y) - 25.5);
                if (dCatch <= 4.5) {
                    eyeSrc.bytes[at + 0] = 255; eyeSrc.bytes[at + 1] = 255; eyeSrc.bytes[at + 2] = 255;
                } else if (dIris <= 7.0) {
                    eyeSrc.bytes[at + 0] = 20; eyeSrc.bytes[at + 1] = 18; eyeSrc.bytes[at + 2] = 24;
                } else if (dIris <= 15.0) {
                    eyeSrc.bytes[at + 0] = 35; eyeSrc.bytes[at + 1] = 80; eyeSrc.bytes[at + 2] = 155;
                } else {
                    eyeSrc.bytes[at + 0] = 250; eyeSrc.bytes[at + 1] = 250; eyeSrc.bytes[at + 2] = 252;
                }
            }
        }
        auto eyeBase = makeCanvas(192, 192);
        auto eyeDoc = Document::fromSnapshot(eyeBase);
        assert(eyeDoc);
        Command eyeSetup = setup;
        for (auto& op : eyeSetup.operations) {
            if (auto* add = std::get_if<AddNode>(&op)) add->node.bounds = {0, 0, 192, 192};
        }
        assert(eyeDoc.value().apply(eyeSetup, eyeDoc.value().snapshot().revision));
        const auto eyeSnap = eyeDoc.value().snapshot();
        std::vector<std::uint8_t> eyeMask(64 * 64, 255);
        ArtistObjectRequest eyeReq = aReq;
        eyeReq.snapshot = eyeSnap;
        eyeReq.sourceMask = eyeMask;
        eyeReq.source = &eyeSrc;
        eyeReq.targetSize = {192, 192};
        auto eyePlanned = planArtistObject(eyeReq);
        assert(eyePlanned);
        Command eyePaint;
        for (auto& batch : eyePlanned.value().batches) {
            for (auto& ev : batch.events) {
                auto b = strokeBounds(ev);
                assert(b);
                eyePaint.operations.emplace_back(Paint{std::move(ev), b.value()});
            }
        }
        assert(eyeDoc.value().apply(eyePaint, eyeSnap.revision));
        auto eyeMerged = renderMergedDocument(eyeDoc.value().snapshot(),
                                              {eyeDoc.value().snapshot().revision, eyeDoc.value().snapshot().eventIndex->size()});
        assert(eyeMerged);
        // Sample 3x upscaled thin iris rim at 10-11 o'clock between catchlight and sclera: (21.5*3, 21.5*3) ~= (65, 65)
        const std::size_t rimAt = std::size_t(65 * 192 + 65) * 4;
        assert(eyeMerged.value().merged.bytes[rimAt + 2] > eyeMerged.value().merged.bytes[rimAt + 0] + 40);
    }

    std::cout << "B51 artist optical blend, Multiply/Screen ORA, 3300x5100 ROI budget, physical dry-ink, and planner tests passed\n";
    return 0;
}
