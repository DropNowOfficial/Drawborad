#include "HeadlessWorkflow.h"
#include "codecs/ExportService.h"
#include "codecs/ImageReader.h"
#include "codecs/OraStream.h"
#include "codecs/RasterWriters.h"
#include "color/ProfileStore.h"
#include "core/ArtistOptIn.h"
#include "core/Commands.h"
#include "reconstruct/ArtistGrammarPlanner.h"
#include "reconstruct/Planning.h"
#include "render/CoverageRasterizer.h"
#include "review/ReviewSession.h"
#include "storage/FileIO.h"
#include "storage/ProjectCodec.h"
#include "storage/ProjectStore.h"
#include "ui/LayerStream.h"
#include "ui/MergedDocument.h"
#include "ui/ReconstructionJob.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace drawborad {
namespace {

Id deterministicStageId(DocumentId docId, NodeId object, std::uint64_t revision, const std::string& stage) {
    Bytes bytes(docId.bytes.begin(), docId.bytes.end());
    bytes.insert(bytes.end(), object.bytes.begin(), object.bytes.end());
    for (unsigned i = 0; i < 8; ++i) bytes.push_back(std::uint8_t(revision >> (8 * i)));
    bytes.insert(bytes.end(), stage.begin(), stage.end());
    const auto hash = sha256(bytes);
    Id id;
    std::copy_n(hash.bytes.begin(), 16, id.bytes.begin());
    return id;
}

std::uint64_t deterministicSeed(Id id) {
    std::uint64_t seed = 0;
    for (unsigned i = 0; i < 8; ++i) seed = (seed << 8) | id.bytes[i];
    return seed;
}

bool pointInPolygon(const std::vector<std::pair<double, double>>& poly, double px, double py) noexcept {
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = poly[i].first, yi = poly[i].second;
        const double xj = poly[j].first, yj = poly[j].second;
        if (((yi > py) != (yj > py)) &&
            (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

void rasterizePolygon4x4ToBucketEvents(
    std::vector<StrokeEvent>& outEvents,
    const std::vector<std::pair<double, double>>& scaledPoly,
    PixelSize canvasSize,
    NodeId objectId,
    NodeId layerId,
    Revision parentRev,
    ColorValue baseColor,
    std::uint32_t caps,
    std::uint64_t seed,
    std::uint64_t& nextTimeUs) {
    if (scaledPoly.size() < 3) return;
    double minX = scaledPoly[0].first, maxX = minX;
    double minY = scaledPoly[0].second, maxY = minY;
    for (const auto& [x, y] : scaledPoly) {
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    }
    const int x0 = std::max(0, int(std::floor(minX)));
    const int y0 = std::max(0, int(std::floor(minY)));
    const int x1 = std::min(int(canvasSize.width), int(std::ceil(maxX)) + 1);
    const int y1 = std::min(int(canvasSize.height), int(std::ceil(maxY)) + 1);
    if (x0 >= x1 || y0 >= y1) return;

    std::array<std::vector<std::array<std::uint32_t, 3>>, 4> tierSpans;
    std::vector<int> rowTier(std::size_t(x1 - x0), -1);
    for (int y = y0; y < y1; ++y) {
        std::fill(rowTier.begin(), rowTier.end(), -1);
        for (int x = x0; x < x1; ++x) {
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy) {
                const double py = double(y) + (double(sy) + 0.5) * 0.25;
                for (int sx = 0; sx < 4; ++sx) {
                    const double px = double(x) + (double(sx) + 0.5) * 0.25;
                    if (pointInPolygon(scaledPoly, px, py)) ++hits;
                }
            }
            if (hits > 0) {
                rowTier[std::size_t(x - x0)] = hits <= 4 ? 0 : (hits <= 8 ? 1 : (hits <= 13 ? 2 : 3));
            }
        }
        int i = 0;
        const int spanW = x1 - x0;
        while (i < spanW) {
            const int t = rowTier[std::size_t(i)];
            if (t < 0) { ++i; continue; }
            int end = i + 1;
            while (end < spanW && rowTier[std::size_t(end)] == t) ++end;
            tierSpans[std::size_t(t)].push_back({std::uint32_t(x0 + i), std::uint32_t(x0 + end), std::uint32_t(y)});
            i = end;
        }
    }
    constexpr std::array<double, 4> alphas{0.25, 0.50, 0.75, 1.00};
    constexpr std::uint32_t CellSide = 1024;
    constexpr std::size_t MaxSpansPerEvent = 60000;
    for (std::size_t t = 0; t < 4; ++t) {
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::array<std::uint32_t, 3>>> cellSpans;
        for (const auto& [sx0, sx1, sy] : tierSpans[t]) {
            if (sx0 >= sx1) continue;
            const std::uint32_t cy = sy / CellSide;
            std::uint32_t curX = sx0;
            while (curX < sx1) {
                const std::uint32_t cx = curX / CellSide;
                const std::uint32_t nextX = std::min(sx1, (cx + 1) * CellSide);
                cellSpans[{cy, cx}].push_back({curX, nextX, sy});
                curX = nextX;
            }
        }
        for (const auto& [cellKey, spans] : cellSpans) {
            (void)cellKey;
            for (std::size_t off = 0; off < spans.size(); off += MaxSpansPerEvent) {
                const std::size_t end = std::min(spans.size(), off + MaxSpansPerEvent);
                StrokeEvent e;
                e.id = deterministicStageId(Id::fromNumber(seed ^ (t + 1)), layerId, outEvents.size() + 1, "poly-span");
                e.objectId = objectId;
                e.layerId = layerId;
                e.parentRevision = parentRev;
                e.engineVersion = 8;
                e.toolId = "bucket";
                e.color = baseColor;
                e.color.alpha = std::clamp(baseColor.alpha * alphas[t], 0.0, 1.0);
                e.seed = seed;
                e.style = StrokeStyle{};
                e.startUs = nextTimeUs;
                e.endUs = nextTimeUs;
                e.geometry.reserve((end - off) * 2);
                for (std::size_t k = off; k < end; ++k) {
                    const auto [sx0, sx1, sy] = spans[k];
                    e.geometry.push_back({double(sx0), double(sy), 1.0, {}, {}, nextTimeUs, 0, caps});
                    e.geometry.push_back({double(sx1), double(sy), 1.0, {}, {}, nextTimeUs, 0, caps});
                }
                nextTimeUs += 50;
                outEvents.push_back(std::move(e));
            }
        }
    }
}

Result<void> saveArtifacts(HeadlessWorkflowReport& report,const std::filesystem::path& output,std::stop_token stop){
    std::error_code ec;
    const auto absolute=std::filesystem::absolute(output,ec);
    if(ec || output.empty() || std::filesystem::exists(absolute,ec))return error(ErrorCode::IoError,"headless.output-exists-or-invalid");
    std::filesystem::create_directories(absolute.parent_path(),ec);
    if(ec)return error(ErrorCode::IoError,"headless.output-parent");
    const auto pattern=(absolute.parent_path()/".drawborad-XXXXXX").string();
    QTemporaryDir staging(QString::fromUtf8(pattern));
    if(!staging.isValid())return error(ErrorCode::IoError,"headless.output-stage");
    const std::filesystem::path directory(staging.path().toUtf8().toStdString());
    const auto& snapshot=report.snapshot;
    auto saved=ProjectStore{}.save(directory/"project.draw",snapshot);if(!saved)return saved.error();
    auto recovered=ProjectStore{}.recover(directory/"project.draw");if(!recovered)return recovered.error();
    if(!recovered.value().snapshot.sameContent(snapshot))return error(ErrorCode::CorruptData,"headless.reopen-mismatch");
    RasterEncodingOptions options;options.ppi=snapshot.spec.ppi;
    if(!snapshot.spec.colorSpec.profileId.empty())options.iccProfile=*snapshot.resources->at(snapshot.spec.colorSpec.profileId).bytes;
    const auto events=snapshot.eventIndex->size();
    const std::array<std::pair<const char*,std::size_t>,3> frames{{{"zero.png",0},{"middle.png",events/2},{"final.png",events}}};
    MergedDocument final;
    std::uint64_t reads=0;
    for(const auto& [name,limit]:frames){
        auto frame=renderMergedDocument(recovered.value().snapshot,{snapshot.revision,limit},{},stop);if(!frame)return frame.error();
        reads+=frame.value().statistics.sourceAssetReads;
        ExportRequest request;request.path=directory/name;request.size=snapshot.spec.pixelSize;
        auto plan=preflightExport(snapshot,request);if(!plan)return plan.error();
        auto written=ExportService{}.write(plan.value(),frame.value().merged,{},options,stop);if(!written)return written.error();
        if(limit==events)final=std::move(frame.value());
    }
    auto beforeSave=renderMergedDocument(snapshot,{snapshot.revision,events},{},stop);if(!beforeSave)return beforeSave.error();
    if(beforeSave.value().merged.bytes!=final.merged.bytes || reads!=0)
        return error(ErrorCode::CorruptData,"headless.replay-mismatch");
    ExportRequest ora;ora.format=ExportFormat::Ora;ora.path=directory/"painted.ora";ora.size=snapshot.spec.pixelSize;
    auto plan=preflightExport(snapshot,ora);if(!plan)return plan.error();
    auto stream=NativeLayerStream::create(recovered.value().snapshot,{snapshot.revision,events},{});
    if(!stream)return stream.error();
    auto exported=writeOraStream(plan.value(),stream.value().size(),
        [&stream](std::size_t index,std::stop_token st){return stream.value().read(index,st);},
        final.merged,options,{},stop);
    if(!exported)return exported.error();
    reads+=stream.value().statistics().sourceAssetReads;
    if(reads!=0)return error(ErrorCode::CorruptData,"headless.replay-mismatch");
    std::size_t leaves=0;for(const auto& [id,node]:*snapshot.nodes){(void)id;if(node.kind==NodeKind::Layer)++leaves;}
    report.metadata.insert("paintLayers",qint64(leaves));
    report.metadata.insert("width",qint64(snapshot.spec.pixelSize.width));report.metadata.insert("height",qint64(snapshot.spec.pixelSize.height));
    report.metadata.insert("dpi",snapshot.spec.ppi);
    report.metadata.insert("savedRevision",QString::number(snapshot.revision));
    report.metadata.insert("replaySourceAssetReads",qint64(reads));report.metadata.insert("reopenReplayMatches",true);
    if(!report.metadata.contains("validationSourceDecodes"))report.metadata.insert("validationSourceDecodes",1);
    if(!report.metadata.contains("reconstructionSourceDecodes"))report.metadata.insert("reconstructionSourceDecodes",1);
    report.metadata.insert("profileSha256",QString::fromStdString(snapshot.spec.colorSpec.profileId.hex()));
    report.metadata.insert("sam3Inference","Not Tested");report.metadata.insert("photoshopOracle","Blocked — Not Run");
    report.metadata.insert("fidelityStatus","NeedsReview");
    report.metadata.insert("qtVersion",qVersion());
#ifdef DRAWBORAD_GIT_SHA
    report.metadata.insert("gitSha",DRAWBORAD_GIT_SHA);
#else
    report.metadata.insert("gitSha","unknown");
#endif
    report.metadata.insert("generatedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    report.metadata.insert("artifacts",QJsonArray{"project.draw","zero.png","middle.png","final.png","painted.ora","report.json"});
    const auto json=QJsonDocument(report.metadata).toJson(QJsonDocument::Indented);
    NativeFileIO io;auto written=io.write(directory/"report.json",std::span(reinterpret_cast<const std::uint8_t*>(json.constData()),std::size_t(json.size())),true);
    if(!written)return written.error();
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"headless.cancelled");
    std::filesystem::rename(directory,absolute,ec);
    if(ec)return error(ErrorCode::IoError,"headless.publish");
    staging.setAutoRemove(false);
    return {};
}

} // namespace

Result<HeadlessWorkflowReport> runHeadlessWorkflow(const HeadlessWorkflowRequest& request,std::stop_token stop) {
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"headless.cancelled");
    if(request.outputDirectory.empty() || std::filesystem::exists(request.outputDirectory))return error(ErrorCode::IoError,"headless.output-exists-or-invalid");
    const QString effectivePlanner = !request.planner.isEmpty() ? request.planner : (request.multiscale ? QStringLiteral("multiscale") : QStringLiteral("sweep"));
    if(effectivePlanner!="sweep" && effectivePlanner!="multiscale" && effectivePlanner!="artist")
        return error(ErrorCode::InvalidInput,"headless.planner");
    const bool artist = (effectivePlanner == "artist");

    const auto& input=request.input;
    const auto size=input.masks.dimensions;
    const auto count=input.masks.fullResolutionMasks.size();
    if((input.backend!="fixture-no-ai" && input.backend!="sam3-image" && input.backend!="opencv-grabcut") ||
       !size.width || !size.height || size.width>8192 || size.height>8192 || size.width*size.height>20000000ull ||
       !count || count>64 || count*size.width*size.height>256ull*1024*1024 ||
       input.proposals.sourceSize!=size || input.proposals.candidates.size()!=count ||
       input.names.size()!=count || input.background.size()!=count || input.scores.size()!=count)
        return error(ErrorCode::InvalidInput,"headless.input-layout");
    if(request.accepted.empty())return error(ErrorCode::InvalidInput,"headless.acceptance-required");
    std::set<std::size_t> accepted;
    for(const auto i:request.accepted)
        if(i>=count || !accepted.insert(i).second)return error(ErrorCode::InvalidInput,"headless.acceptance-index");

    const PixelSize outSize = (request.targetSize.width && request.targetSize.height) ? request.targetSize : size;
    const double outDpi = (std::isfinite(request.targetDpi) && request.targetDpi > 0.0) ? request.targetDpi : 300.0;
    if(!outSize.width || !outSize.height || outSize.width>8192 || outSize.height>8192 || outSize.width*outSize.height>20000000ull)
        return error(ErrorCode::BudgetExceeded,"headless.target-size");
    const bool superRes = (outSize != size);
    if(superRes && !artist)
        return error(ErrorCode::UnsupportedFeature,"headless.superres-requires-artist");

    Bytes occupied(std::size_t(size.width*size.height));
    for(std::size_t i=0;i<count;++i){
        const auto& mask=input.masks.fullResolutionMasks[i];
        if(mask.size()!=occupied.size() || input.names[i].isEmpty() || input.names[i].size()>128 ||
           !std::isfinite(input.scores[i]) || input.scores[i]<0 || input.scores[i]>1)
            return error(ErrorCode::InvalidInput,"headless.object-layout");
        bool nonempty=false;
        for(std::size_t p=0;p<mask.size();++p){
            if(mask[p]!=0 && mask[p]!=255)return error(ErrorCode::InvalidInput,"headless.mask-binary");
            if(mask[p]){
                if(occupied[p])return error(ErrorCode::InvalidInput,"headless.mask-overlap");
                occupied[p]=255;nonempty=true;
            }
        }
        if(!nonempty)return error(ErrorCode::InvalidInput,"headless.mask-empty");
    }
    const auto sourcePath=input.sourceReference.value("image").toString();
#ifdef _WIN32
    const auto nativePath=std::filesystem::path(sourcePath.toStdWString());
#else
    const auto nativePath=std::filesystem::path(sourcePath.toUtf8().toStdString());
#endif
    auto source=decodeImage(nativePath,{},stop);if(!source)return source.error();
    if(!source.value().decodedPixels || source.value().info.orientedSize!=size ||
       input.sourceReference.value("type").toString()!=SourceReferenceType ||
       input.sourceReference.value("sourceHash").toString().toStdString()!=source.value().info.sourceHash.hex() ||
       input.sourceReference.value("modelBackend").toString()!=input.backend)
        return error(ErrorCode::DigestMismatch,"headless.source-verification");
    auto pixels=std::move(*source.value().decodedPixels);
    if(pixels.format.mode!=ColorMode::RGB || pixels.format.depth!=Depth::U8)
        return error(ErrorCode::UnsupportedFeature,"headless.rgb8-required");
    for(std::uint64_t y=0;y<size.height;++y)for(std::uint64_t x=0;x<size.width;++x){
        const bool visible=!pixels.format.alpha || pixels.bytes[std::size_t(y*pixels.rowStride+x*4+3)]!=0;
        if((occupied[std::size_t(y*size.width+x)]!=0)!=visible)
            return error(ErrorCode::InvalidInput,"headless.source-coverage");
    }
    if(!pixels.format.alpha){
        Bytes rgba(std::size_t(size.width*size.height*4),255);
        for(std::uint64_t y=0;y<size.height;++y)for(std::uint64_t x=0;x<size.width;++x)
            std::copy_n(pixels.bytes.begin()+std::ptrdiff_t(y*pixels.rowStride+x*3),3,rgba.begin()+std::ptrdiff_t((y*size.width+x)*4));
        pixels.format.alpha=true;pixels.rowStride=size.width*4;pixels.bytes=std::move(rgba);
    }

    const bool amodal = (request.amodalBackground || (artist && input.amodalBackground)) &&
                        input.background[0] && accepted.contains(0) && accepted.size() >= 2;

    // Native continuous super-resolution path (or ultra-large >4MiP artist canvas where
    // 4x4 supersampled vector silhouettes replace low-res binary bitmap object masks).
    if(superRes || (artist && outSize.width*outSize.height > 4ull*1024*1024)){
        NewDocumentRequest create;
        create.dimensions={double(outSize.width),double(outSize.height),Unit::Px,outDpi};
        create.colorSpec.profileId=source.value().info.icc;
        auto spec=validateDocument(create,{});if(!spec)return spec.error();
        Id documentId;std::copy_n(source.value().info.sourceHash.bytes.begin(),16,documentId.bytes.begin());
        auto document=Document::create(spec.value(),documentId);if(!document)return document.error();

        Command setup;
        if(!source.value().info.icc.empty()){
            const auto& bytes=source.value().info.iccProfile;auto profile=inspectIcc(bytes);
            if(!profile || profile.value().colorSpace!=IccColorSpace::RGB || sha256(bytes)!=source.value().info.icc)
                return error(ErrorCode::InvalidProfile,"headless.source-profile");
            setup.operations.emplace_back(InstallResource{Resource{source.value().info.icc,ResourceKind::Profile,std::make_shared<const Bytes>(bytes),{}}});
        }

        std::optional<TileData> amodalBg;
        if(amodal){
            auto inp = inpaintAmodalBackground(pixels, input.masks.fullResolutionMasks[0], stop);
            if(!inp) return inp.error();
            amodalBg = std::move(inp.value());
        }

        struct ArtistStageSpec {
            const char* stage;
            const char* name;
            BlendMode blend;
            double opacity;
        };
        constexpr std::array<ArtistStageSpec,5> stageSpecs{{
            {"artist.flats","Flats · Silhouette",BlendMode::Normal,1.0},
            {"artist.form_glaze","Form & Shadow Glaze",BlendMode::Multiply,0.85},
            {"artist.structure","Surface Structure",BlendMode::Normal,1.0},
            {"artist.hero_ink","Hero Lineart & Dry Ink",BlendMode::Normal,1.0},
            {"artist.highlights","Highlights & Accents",BlendMode::Screen,0.90}
        }};

        QJsonArray names, objectResults;
        std::vector<std::size_t> orderedAccepted(accepted.begin(), accepted.end());
        for(const auto idx : orderedAccepted){
            LayerNode object;object.id=Id::fromNumber(10+idx*2);object.kind=NodeKind::Object;
            object.parent=Document::rootId();object.name=input.names[idx].toUtf8().toStdString();
            object.bounds={0,0,outSize.width,outSize.height};
            setup.operations.emplace_back(AddNode{object});
            for(std::size_t sIdx=0;sIdx<stageSpecs.size();++sIdx){
                const auto& sp=stageSpecs[sIdx];
                LayerNode layer;
                layer.id=deterministicStageId(documentId,object.id,0,sp.stage);
                layer.parent=object.id;layer.objectId=object.id;
                layer.name=sp.name;layer.stage=sp.stage;layer.blend=sp.blend;layer.opacity=sp.opacity;
                layer.bounds=object.bounds;
                setup.operations.emplace_back(AddNode{layer,sIdx});
            }
            names.append(input.names[idx]);
            objectResults.append(QJsonObject{{"name",input.names[idx]},{"status","needs-review"}});
        }
        auto installed=document.value().apply(setup,0);if(!installed)return installed.error();
        const auto planningSnap=document.value().snapshot();

        Command paintCmd;
        std::uint64_t nextTime=100;
        const std::size_t perObjCap=std::max<std::size_t>(512,56000/std::max<std::size_t>(1,orderedAccepted.size()));
        for(const auto idx : orderedAccepted){
            if(stop.stop_requested())return error(ErrorCode::Cancelled,"headless.cancelled");
            const NodeId objId=Id::fromNumber(10+idx*2);
            const Bytes& maskToUse = (idx==0 && amodal) ? occupied : input.masks.fullResolutionMasks[idx];
            const TileData& srcToUse = (idx==0 && amodalBg.has_value()) ? *amodalBg : pixels;
            ArtistObjectRequest aReq;
            aReq.snapshot=planningSnap;
            aReq.objectId=objId;
            aReq.layers={
                deterministicStageId(documentId,objId,0,"artist.flats"),
                deterministicStageId(documentId,objId,0,"artist.form_glaze"),
                deterministicStageId(documentId,objId,0,"artist.structure"),
                deterministicStageId(documentId,objId,0,"artist.hero_ink"),
                deterministicStageId(documentId,objId,0,"artist.highlights")
            };
            aReq.sourceMask=maskToUse;
            aReq.source=&srcToUse;
            aReq.targetSize=outSize;
            aReq.clipToObjectMask=false;
            aReq.seed=deterministicSeed(deterministicStageId(documentId,objId,0,"artist.seed"));
            aReq.maxTotalEvents=perObjCap;
            aReq.maxTotalSamples=1000000;
            aReq.stop=stop;
            auto planned=planArtistObject(aReq);if(!planned)return planned.error();
            for(auto& batch : planned.value().batches){
                SchedulePolicy policy;
                policy.seed=deterministicSeed(deterministicStageId(documentId,batch.layerId,0,"schedule"));
                policy.startUs=nextTime;
                policy.maxEvents=32768;
                auto scheduled=scheduleStrokes(batch,policy);if(!scheduled)return scheduled.error();
                nextTime=scheduled.value().events.back().endUs+100;
                for(auto& ev : scheduled.value().events){
                    auto b=strokeBounds(ev);if(!b)return b.error();
                    paintCmd.operations.emplace_back(Paint{std::move(ev),b.value()});
                }
            }
        }
        auto committed=document.value().apply(paintCmd,planningSnap.revision);if(!committed)return committed.error();
        HeadlessWorkflowReport report;
        report.snapshot=document.value().snapshot();
        report.metadata=QJsonObject{
            {"type",ReconstructionResultType},
            {"mode","reconstruct"},
            {"planner",effectivePlanner},
            {"amodalBackground",amodal},
            {"sourceWidth",qint64(size.width)},
            {"sourceHeight",qint64(size.height)},
            {"superResolution",superRes},
            {"sourceHash",QString::fromStdString(source.value().info.sourceHash.hex())},
            {"objects",objectResults},
            {"backend",input.backend},
            {"acceptedObjects",names},
            {"strokeEvents",qint64(report.snapshot.eventIndex->size())},
            {"objectCount",qint64(accepted.size())},
            {"wholeImageFidelityVerified",false},
            {"naturalProcessReviewed",false}
        };
        auto artifacts=saveArtifacts(report,request.outputDirectory,stop);if(!artifacts)return artifacts.error();
        return report;
    }

    auto review=ReviewSession::create(input.proposals,input.masks,0);if(!review)return review.error();
    auto reviewed=review.value().accept(0);if(!reviewed)return reviewed.error();
    NewDocumentRequest create;create.dimensions={double(size.width),double(size.height),Unit::Px,outDpi};
    create.colorSpec.profileId=source.value().info.icc;
    auto spec=validateDocument(create,{});if(!spec)return spec.error();
    Id documentId;std::copy_n(source.value().info.sourceHash.bytes.begin(),16,documentId.bytes.begin());
    auto document=Document::create(spec.value(),documentId);if(!document)return document.error();
    const auto reference=jsonResource(input.sourceReference);
    Command setup;setup.operations.emplace_back(InstallResource{reference});
    if(!source.value().info.icc.empty()){
        const auto& bytes=source.value().info.iccProfile;auto profile=inspectIcc(bytes);
        if(!profile || profile.value().colorSpace!=IccColorSpace::RGB || sha256(bytes)!=source.value().info.icc)
            return error(ErrorCode::InvalidProfile,"headless.source-profile");
        setup.operations.emplace_back(InstallResource{Resource{source.value().info.icc,ResourceKind::Profile,std::make_shared<const Bytes>(bytes),{}}});
    }
    QJsonArray names;
    // Preserve manifest's back-to-front order, independent of selection order.
    for(std::size_t i=0;i<count;++i){
        if(!accepted.contains(i))continue;
        const Bytes& bytes = (i==0 && amodal) ? occupied : reviewed.value().masks[i];
        Resource mask{sha256(bytes),ResourceKind::Mask,std::make_shared<const Bytes>(bytes),{reference.id}};
        setup.operations.emplace_back(InstallResource{mask});
        LayerNode object;object.id=Id::fromNumber(10+i*2);object.kind=NodeKind::Object;
        object.parent=Document::rootId();object.name=input.names[i].toUtf8().toStdString();object.maskId=mask.id;
        object.bounds={0,0,size.width,size.height};
        setup.operations.emplace_back(AddNode{object});
        if(!artist){
            LayerNode paint;paint.id=Id::fromNumber(11+i*2);paint.parent=object.id;paint.objectId=object.id;
            paint.name=object.name+" / Paint";paint.stage="paint";paint.bounds=object.bounds;
            setup.operations.emplace_back(AddNode{paint});
        }
        names.append(input.names[i]);
    }
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"headless.cancelled");
    auto installed=document.value().apply(setup,0);if(!installed)return installed.error();
    const auto before=document.value().snapshot();auto job=before;
    auto resources=std::make_shared<ResourceMap>(*before.resources);
    const auto instruction=jsonResource({{"type",ReconstructionRequestType},{"mode","reconstruct"},{"image",sourcePath},
        {"planner",effectivePlanner},{"amodalBackground",amodal}});
    resources->emplace(instruction.id,instruction);job.resources=resources;
    auto delta=runReconstructionJob(job,stop);if(!delta)return delta.error();
    auto after=decodeDelta(delta.value(),job);if(!after)return after.error();
    auto command=reconstructionCommand(before,after.value());if(!command)return command.error();
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"headless.cancelled");
    auto committed=document.value().apply(command.value(),before.revision);if(!committed)return committed.error();
    HeadlessWorkflowReport report;report.snapshot=document.value().snapshot();
    report.metadata=findJsonResource(report.snapshot,ReconstructionResultType).value_or(QJsonObject{});
    report.metadata.insert("backend",input.backend);report.metadata.insert("acceptedObjects",names);
    report.metadata.insert("amodalBackground",amodal);
    report.metadata.insert("strokeEvents",qint64(report.snapshot.eventIndex->size()));
    report.metadata.insert("objectCount",qint64(accepted.size()));
    auto artifacts=saveArtifacts(report,request.outputDirectory,stop);if(!artifacts)return artifacts.error();
    return report;
}

Result<HeadlessWorkflowReport> compileHeadlessPlan(const HeadlessCompilePlanRequest& request, std::stop_token stop) {
    if (stop.stop_requested()) return error(ErrorCode::Cancelled, "compile-plan.cancelled");
    if (request.planPath.empty() || request.outputDirectory.empty() ||
        std::filesystem::exists(request.outputDirectory))
        return error(ErrorCode::IoError, "compile-plan.paths");

    NativeFileIO io;
    auto rawPlan = io.read(request.planPath, 128 * 1024 * 1024);
    if (!rawPlan) return rawPlan.error();
    QJsonParseError parseErr{};
    const auto doc = QJsonDocument::fromJson(
        QByteArray(reinterpret_cast<const char*>(rawPlan.value().data()), qsizetype(rawPlan.value().size())), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject())
        return error(ErrorCode::CorruptData, "compile-plan.json");

    const auto root = doc.object();
    if (root.value("schema").toString() != QLatin1String("drawborad.stroke-plan.v1"))
        return error(ErrorCode::UnsupportedFeature, "compile-plan.schema");

    const auto planW = root.value("width").toInteger(-1);
    const auto planH = root.value("height").toInteger(-1);
    if (planW <= 0 || planH <= 0 || planW > 8192 || planH > 8192 || planW * planH > 20000000)
        return error(ErrorCode::InvalidInput, "compile-plan.dimensions");

    const PixelSize targetSize = (request.targetSize.width && request.targetSize.height)
                                     ? request.targetSize
                                     : PixelSize{std::uint64_t(planW), std::uint64_t(planH)};
    if (!targetSize.width || !targetSize.height || targetSize.width > 8192 ||
        targetSize.height > 8192 || targetSize.width * targetSize.height > 20000000ull)
        return error(ErrorCode::BudgetExceeded, "compile-plan.target-dimensions");

    const double dpi = (std::isfinite(request.targetDpi) && request.targetDpi > 0.0)
                           ? request.targetDpi
                           : root.value("dpi").toDouble(300.0);
    if (!std::isfinite(dpi) || dpi <= 0.0 || dpi > 10000.0)
        return error(ErrorCode::InvalidInput, "compile-plan.dpi");

    const double scaleX = double(targetSize.width) / double(planW);
    const double scaleY = double(targetSize.height) / double(planH);
    const double scaleW = std::sqrt(scaleX * scaleY);

    NewDocumentRequest create;
    create.dimensions = {double(targetSize.width), double(targetSize.height), Unit::Px, dpi};
    if (root.value("background").isObject()) {
        const auto bg = root.value("background").toObject();
        create.transparent = bg.value("transparent").toBool(true);
        if (!create.transparent && bg.value("paperRgb").isArray()) {
            const auto arr = bg.value("paperRgb").toArray();
            if (arr.size() == 3) {
                create.paperColor.channels = {
                    std::clamp(arr[0].toDouble(1.0), 0.0, 1.0),
                    std::clamp(arr[1].toDouble(1.0), 0.0, 1.0),
                    std::clamp(arr[2].toDouble(1.0), 0.0, 1.0), 0.0};
                create.paperColor.alpha = 1.0;
            }
        }
    }
    auto spec = validateDocument(create, {});
    if (!spec) return spec.error();
    const auto planDigest = sha256(rawPlan.value());
    Id docId;
    std::copy_n(planDigest.bytes.begin(), 16, docId.bytes.begin());
    auto document = Document::create(spec.value(), docId);
    if (!document) return document.error();

    const auto objectsArr = root.value("objects").toArray();
    if (objectsArr.isEmpty() || objectsArr.size() > 128)
        return error(ErrorCode::InvalidInput, "compile-plan.objects");

    Command setupCmd;
    QJsonArray acceptedNames;
    struct LayerBuildSpec {
        NodeId objectId;
        NodeId layerId;
        QJsonArray strokes;
    };
    std::vector<LayerBuildSpec> layerSpecs;
    std::size_t nextNodeNum = 10;

    for (qsizetype oi = 0; oi < objectsArr.size(); ++oi) {
        if (!objectsArr[oi].isObject()) return error(ErrorCode::InvalidInput, "compile-plan.object");
        const auto objJson = objectsArr[oi].toObject();
        const auto objName = objJson.value("name").toString().trimmed();
        if (objName.isEmpty() || objName.size() > 128)
            return error(ErrorCode::InvalidInput, "compile-plan.object-name");
        acceptedNames.append(objName);

        LayerNode objNode;
        objNode.id = Id::fromNumber(nextNodeNum++);
        objNode.kind = NodeKind::Object;
        objNode.parent = Document::rootId();
        objNode.name = objName.toUtf8().toStdString();
        objNode.visible = objJson.value("visible").toBool(true);
        objNode.bounds = {0, 0, targetSize.width, targetSize.height};
        setupCmd.operations.emplace_back(AddNode{objNode});

        const auto layersArr = objJson.value("layers").toArray();
        if (layersArr.isEmpty() || layersArr.size() > 32)
            return error(ErrorCode::InvalidInput, "compile-plan.layers");

        for (qsizetype li = 0; li < layersArr.size(); ++li) {
            if (!layersArr[li].isObject()) return error(ErrorCode::InvalidInput, "compile-plan.layer");
            const auto lJson = layersArr[li].toObject();
            const auto lName = lJson.value("name").toString("Paint").trimmed();
            const auto stage = lJson.value("stage").toString("artist.structure").trimmed();
            const auto blendStr = lJson.value("blend").toString("normal").toLower();
            if (lName.isEmpty() || lName.size() > 128 || stage.isEmpty() || stage.size() > 128)
                return error(ErrorCode::InvalidInput, "compile-plan.layer-meta");

            BlendMode blend = BlendMode::Normal;
            if (blendStr == "multiply") blend = BlendMode::Multiply;
            else if (blendStr == "screen") blend = BlendMode::Screen;
            else if (blendStr != "normal") return error(ErrorCode::UnsupportedFeature, "compile-plan.blend");

            if (lJson.contains("opacity") && !lJson.value("opacity").isDouble())
                return error(ErrorCode::InvalidInput, "compile-plan.opacity");
            const double opacity = lJson.value("opacity").toDouble(1.0);
            if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0)
                return error(ErrorCode::InvalidInput, "compile-plan.opacity");

            LayerNode layerNode;
            layerNode.id = Id::fromNumber(nextNodeNum++);
            layerNode.kind = NodeKind::Layer;
            layerNode.parent = objNode.id;
            layerNode.objectId = objNode.id;
            layerNode.name = lName.toUtf8().toStdString();
            layerNode.stage = stage.toUtf8().toStdString();
            layerNode.blend = blend;
            layerNode.opacity = opacity;
            layerNode.visible = lJson.value("visible").toBool(true);
            layerNode.bounds = objNode.bounds;
            setupCmd.operations.emplace_back(AddNode{layerNode, std::size_t(li)});
            layerSpecs.push_back({objNode.id, layerNode.id, lJson.value("strokes").toArray()});
        }
    }

    auto appliedSetup = document.value().apply(setupCmd, 0);
    if (!appliedSetup) return appliedSetup.error();
    const Revision baseRev = document.value().snapshot().revision;

    Command paintCmd;
    std::uint64_t nextTimeUs = 100;
    std::uint64_t strokeSerial = 1;
    const double maxTargetX = std::nextafter(double(targetSize.width), 0.0);
    const double maxTargetY = std::nextafter(double(targetSize.height), 0.0);

    for (const auto& ls : layerSpecs) {
        for (qsizetype si = 0; si < ls.strokes.size(); ++si) {
            if (stop.stop_requested()) return error(ErrorCode::Cancelled, "compile-plan.cancelled");
            if (!ls.strokes[si].isObject()) return error(ErrorCode::InvalidInput, "compile-plan.stroke");
            const auto sJson = ls.strokes[si].toObject();
            const auto engine = std::uint32_t(sJson.value("engine").toInt(19));
            const auto tool = sJson.value("tool").toString(
                engine == 8 ? "bucket" : engine == 12 ? "dry-ink" : engine == 21 ? "chisel-ink-aa" :
                engine == 10 ? "fineliner-aa" : engine == 2 ? "marker" : "smooth-ink-aa");

            const auto colorArr = sJson.value("color").toArray();
            if (colorArr.size() < 3 || colorArr.size() > 4)
                return error(ErrorCode::InvalidInput, "compile-plan.color");
            ColorValue color;
            color.activeChannels = 3;
            for (int c = 0; c < 3; ++c) {
                if (!colorArr[c].isDouble()) return error(ErrorCode::InvalidInput, "compile-plan.color");
                const double cv = colorArr[c].toDouble();
                if (!std::isfinite(cv)) return error(ErrorCode::InvalidInput, "compile-plan.color");
                color.channels[std::size_t(c)] = std::clamp(cv, 0.0, 1.0);
            }
            if (colorArr.size() == 4) {
                if (!colorArr[3].isDouble()) return error(ErrorCode::InvalidInput, "compile-plan.color");
                const double ca = colorArr[3].toDouble();
                if (!std::isfinite(ca)) return error(ErrorCode::InvalidInput, "compile-plan.color");
                color.alpha = std::clamp(ca, 0.0, 1.0);
            } else {
                color.alpha = 1.0;
            }

            std::uint32_t caps = 0;
            if (sJson.value("linearOptical").toBool(true)) caps |= StrokeCapabilityLinearOptical;
            if (sJson.value("physicalDryInk").toBool(engine == 12)) caps |= StrokeCapabilityPhysicalDryInk;

            const auto seed = std::uint64_t(sJson.value("seed").toInteger(qint64(strokeSerial * 2654435761ull)));

            if (engine == 8) {
                if (sJson.value("polygon").isArray()) {
                    const auto polyArr = sJson.value("polygon").toArray();
                    if (polyArr.size() < 3 || polyArr.size() > 16384)
                        return error(ErrorCode::InvalidInput, "compile-plan.polygon");
                    std::vector<std::pair<double, double>> poly;
                    poly.reserve(std::size_t(polyArr.size()));
                    for (const auto& pv : polyArr) {
                        const auto pa = pv.toArray();
                        if (pa.size() != 2 || !pa[0].isDouble() || !pa[1].isDouble())
                            return error(ErrorCode::InvalidInput, "compile-plan.polygon");
                        const double vx = pa[0].toDouble(), vy = pa[1].toDouble();
                        if (!std::isfinite(vx) || !std::isfinite(vy) ||
                            vx < -8192.0 || vx > 16384.0 || vy < -8192.0 || vy > 16384.0)
                            return error(ErrorCode::InvalidInput, "compile-plan.polygon");
                        poly.emplace_back(vx * scaleX, vy * scaleY);
                    }
                    std::vector<StrokeEvent> polyEvents;
                    rasterizePolygon4x4ToBucketEvents(polyEvents, poly, targetSize, ls.objectId, ls.layerId,
                                                      baseRev, color, caps, seed + strokeSerial++, nextTimeUs);
                    for (auto& pe : polyEvents) {
                        auto b = strokeBounds(pe);
                        if (!b) return b.error();
                        paintCmd.operations.emplace_back(Paint{std::move(pe), b.value()});
                    }
                    continue;
                }
                const auto spansArr = sJson.value("spans").toArray();
                if (spansArr.isEmpty()) continue;
                if (spansArr.size() > 500000)
                    return error(ErrorCode::BudgetExceeded, "compile-plan.spans");
                std::vector<std::array<std::uint32_t, 3>> scaledSpans;
                for (const auto& sv : spansArr) {
                    const auto sa = sv.toArray();
                    if (sa.size() != 3 || !sa[0].isDouble() || !sa[1].isDouble() || !sa[2].isDouble())
                        return error(ErrorCode::InvalidInput, "compile-plan.span");
                    const double rx0 = sa[0].toDouble(), rx1 = sa[1].toDouble(), ry = sa[2].toDouble();
                    if (!std::isfinite(rx0) || !std::isfinite(rx1) || !std::isfinite(ry) ||
                        rx0 < -8192.0 || rx0 > 16384.0 || rx1 < -8192.0 || rx1 > 16384.0 ||
                        ry < -8192.0 || ry > 16384.0 || rx1 < rx0)
                        return error(ErrorCode::InvalidInput, "compile-plan.span");
                    const int sx0 = std::clamp(int(std::floor(rx0 * scaleX)), 0, int(targetSize.width));
                    const int sx1 = std::clamp(int(std::ceil(rx1 * scaleX)), 0, int(targetSize.width));
                    const int sy0 = std::clamp(int(std::floor(ry * scaleY)), 0, int(targetSize.height));
                    const int sy1 = std::clamp(int(std::ceil((ry + 1.0) * scaleY)), sy0 + 1, int(targetSize.height));
                    if (sx0 >= sx1) continue;
                    for (int y = sy0; y < sy1; ++y) {
                        scaledSpans.push_back({std::uint32_t(sx0), std::uint32_t(sx1), std::uint32_t(y)});
                    }
                }
                if (scaledSpans.empty()) continue;
                std::sort(scaledSpans.begin(), scaledSpans.end(), [](const auto& a, const auto& b) {
                    return a[2] != b[2] ? a[2] < b[2] : (a[0] != b[0] ? a[0] < b[0] : a[1] < b[1]);
                });
                std::vector<std::array<std::uint32_t, 3>> mergedSpans;
                for (const auto& sp : scaledSpans) {
                    if (!mergedSpans.empty() && mergedSpans.back()[2] == sp[2] && sp[0] <= mergedSpans.back()[1]) {
                        mergedSpans.back()[1] = std::max(mergedSpans.back()[1], sp[1]);
                    } else {
                        mergedSpans.push_back(sp);
                    }
                }
                constexpr std::uint32_t CellSide = 1024;
                std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::array<std::uint32_t, 3>>> cellSpans;
                for (const auto& [sx0, sx1, sy] : mergedSpans) {
                    const std::uint32_t cy = sy / CellSide;
                    std::uint32_t curX = sx0;
                    while (curX < sx1) {
                        const std::uint32_t cx = curX / CellSide;
                        const std::uint32_t nextX = std::min(sx1, (cx + 1) * CellSide);
                        cellSpans[{cy, cx}].push_back({curX, nextX, sy});
                        curX = nextX;
                    }
                }
                constexpr std::size_t MaxSpans = 60000;
                for (const auto& [cellKey, cSpans] : cellSpans) {
                    (void)cellKey;
                    for (std::size_t off = 0; off < cSpans.size(); off += MaxSpans) {
                        const std::size_t end = std::min(cSpans.size(), off + MaxSpans);
                        StrokeEvent ev;
                        ev.id = deterministicStageId(docId, ls.layerId, strokeSerial++, "span");
                        ev.objectId = ls.objectId;
                        ev.layerId = ls.layerId;
                        ev.parentRevision = baseRev;
                        ev.engineVersion = 8;
                        ev.toolId = "bucket";
                        ev.color = color;
                        ev.seed = seed;
                        ev.style = StrokeStyle{};
                        ev.startUs = nextTimeUs;
                        ev.endUs = nextTimeUs;
                        for (std::size_t k = off; k < end; ++k) {
                            const auto [x0, x1, y] = cSpans[k];
                            ev.geometry.push_back({double(x0), double(y), 1.0, {}, {}, nextTimeUs, 0, caps});
                            ev.geometry.push_back({double(x1), double(y), 1.0, {}, {}, nextTimeUs, 0, caps});
                        }
                        nextTimeUs += 50;
                        auto b = strokeBounds(ev);
                        if (!b) return b.error();
                        paintCmd.operations.emplace_back(Paint{std::move(ev), b.value()});
                    }
                }
                continue;
            }

            if (engine != 2 && engine != 10 && engine != 12 && engine != 19 && engine != 21)
                return error(ErrorCode::UnsupportedFeature, "compile-plan.engine");

            const auto ptsArr = sJson.value("points").toArray();
            if (ptsArr.isEmpty() || ptsArr.size() > 4096)
                return error(ErrorCode::InvalidInput, "compile-plan.points");

            StrokeEvent ev;
            ev.id = deterministicStageId(docId, ls.layerId, strokeSerial++, "stroke");
            ev.objectId = ls.objectId;
            ev.layerId = ls.layerId;
            ev.parentRevision = baseRev;
            ev.engineVersion = engine;
            ev.toolId = tool.toUtf8().toStdString();
            ev.color = color;
            ev.seed = engine == 21 ? 0 : seed;

            if (sJson.contains("width") && !sJson.value("width").isDouble())
                return error(ErrorCode::InvalidInput, "compile-plan.width");
            if (sJson.contains("opacity") && !sJson.value("opacity").isDouble())
                return error(ErrorCode::InvalidInput, "compile-plan.opacity");
            if (sJson.contains("hardness") && !sJson.value("hardness").isDouble())
                return error(ErrorCode::InvalidInput, "compile-plan.hardness");
            const double rawWUnscaled = sJson.value("width").toDouble(2.0);
            const double rawOp = sJson.value("opacity").toDouble(1.0);
            const double rawHard = sJson.value("hardness").toDouble(1.0);
            if (!std::isfinite(rawWUnscaled) || rawWUnscaled <= 0.0 || rawWUnscaled > 8192.0 ||
                !std::isfinite(rawOp) || rawOp < 0.0 || rawOp > 1.0 ||
                !std::isfinite(rawHard) || rawHard < 0.0 || rawHard > 1.0)
                return error(ErrorCode::InvalidInput, "compile-plan.style");

            const double rawWidth = rawWUnscaled * scaleW;
            const double minW = engine == 21 ? 0.5 : (engine == 12 ? 0.25 : 0.05);
            const double maxW = engine == 10 ? 8.0 : 128.0;
            ev.style.width = std::clamp(rawWidth, minW, maxW);
            ev.style.opacity = std::clamp(rawOp, 0.0, 1.0);
            ev.style.hardness = (engine == 2) ? std::clamp(rawHard, 0.0, 1.0) : 1.0;
            ev.style.shape = (engine == 21) ? NibShape::Chisel : NibShape::Round;
            ev.style.angle = (engine == 21) ? std::uint8_t(std::clamp(sJson.value("angle").toInt(0), 0, 7)) : 0;

            ev.startUs = nextTimeUs;
            for (const auto& pv : ptsArr) {
                const auto pa = pv.toArray();
                if (pa.size() < 2 || pa.size() > 3 || !pa[0].isDouble() || !pa[1].isDouble() ||
                    (pa.size() == 3 && !pa[2].isDouble()))
                    return error(ErrorCode::InvalidInput, "compile-plan.point");
                const double rawX = pa[0].toDouble(), rawY = pa[1].toDouble();
                const double rawP = pa.size() == 3 ? pa[2].toDouble() : 1.0;
                if (!std::isfinite(rawX) || !std::isfinite(rawY) || !std::isfinite(rawP) ||
                    rawX < -8192.0 || rawX > 16384.0 || rawY < -8192.0 || rawY > 16384.0)
                    return error(ErrorCode::InvalidInput, "compile-plan.point");
                const double px = std::clamp(rawX * scaleX, 0.0, maxTargetX);
                const double py = std::clamp(rawY * scaleY, 0.0, maxTargetY);
                const double pr = std::clamp(rawP, 0.0, 1.0);
                ev.geometry.push_back({px, py, pr, {}, {}, nextTimeUs, 0, caps});
                nextTimeUs += 100;
            }
            ev.endUs = ev.geometry.back().timestampUs;
            nextTimeUs += 50;

            auto b = strokeBounds(ev);
            if (!b) return b.error();
            paintCmd.operations.emplace_back(Paint{std::move(ev), b.value()});
        }
    }

    if (paintCmd.operations.empty())
        return error(ErrorCode::InvalidInput, "compile-plan.empty-strokes");
    if (paintCmd.operations.size() > 65536)
        return error(ErrorCode::BudgetExceeded, "compile-plan.stroke-budget");

    auto appliedPaint = document.value().apply(paintCmd, baseRev);
    if (!appliedPaint) return appliedPaint.error();

    const auto finalSnap = document.value().snapshot();
    auto mergedPlan = planMergedDocument(finalSnap);
    if (!mergedPlan) return mergedPlan.error();

    HeadlessWorkflowReport report;
    report.snapshot = finalSnap;
    report.metadata = QJsonObject{
        {"mode", "compile-plan"},
        {"planner", root.value("planner").toString("differentiable-artist")},
        {"backend", root.value("backend").toString("compile-plan")},
        {"acceptedObjects", acceptedNames},
        {"strokeEvents", qint64(finalSnap.eventIndex->size())},
        {"objectCount", qint64(acceptedNames.size())},
        {"validationSourceDecodes", 0},
        {"reconstructionSourceDecodes", 0},
        {"wholeImageFidelityVerified", false},
        {"naturalProcessReviewed", false}
    };
    auto artifacts = saveArtifacts(report, request.outputDirectory, stop);
    if (!artifacts) return artifacts.error();
    return report;
}

} // namespace drawborad
