#include "headless/HeadlessWorkflow.h"
#include "headless/ProjectSession.h"
#include "codecs/ImageReader.h"
#include "ui/ReviewJob.h"
#include <QCoreApplication>
#include <QImage>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <numeric>
#include <limits>
using namespace drawborad;
namespace {
// This command-owned fixture never goes through or weakens the SAM manifest loader.
Result<SemanticManifestImport> smokeFixture(const QString& path){
    QImage image(64,64,QImage::Format_RGBA8888);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)
        image.setPixelColor(x,y,x<32?QColor(180,60,40):QColor(40,100,180));
    if(!image.save(path))return error(ErrorCode::IoError,"smoke.source-write");
    auto decoded=decodeImage(std::filesystem::path(path.toUtf8().toStdString()),{});if(!decoded)return decoded.error();
    SemanticManifestImport input;input.backend="fixture-no-ai";
    input.masks.dimensions={64,64};input.proposals.sourceSize={64,64};
    input.sourceReference={{"type",SourceReferenceType},{"image",path},{"width",64},{"height",64},
        {"sourceHash",QString::fromStdString(decoded.value().info.sourceHash.hex())},{"modelBackend",input.backend}};
    for(unsigned i=0;i<2;++i){
        Bytes mask(4096);for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)mask[y*64+x]=((x<32)==(i==0))?255:0;
        input.masks.fullResolutionMasks.push_back(std::move(mask));
        input.proposals.candidates.push_back({Id::fromNumber(i+1),{0,0,64,64},{},1});
        input.names.push_back(i==0?"Left":"Right");input.scores.push_back(1);input.background.push_back(false);
    }
    return input;
}
int execute(const QStringList& args){
    if(args.size()==2 && args[1]=="session")return runProjectSession();
    QTextStream out(stdout),err(stderr);
    if(args.size()==2 && (args[1]=="--help" || args[1]=="-h")){
        out<<"DrawboradHeadless\nUsage:\n"
              "  DrawboradHeadless run --manifest PATH --output DIR --accept-all\n"
              "  DrawboradHeadless run --manifest PATH --output DIR --accept-index N [--accept-index N] [--fixture]\n"
              "  DrawboradHeadless smoke --output DIR\n"
              "  Optional: --planner sweep|multiscale (multiscale requires visual review)\n"
              "Output must be a new directory. Smoke provenance: fixture-no-ai.\n";
        return 0;
    }
    const auto invalid=[&](){err<<"Invalid arguments. Run DrawboradHeadless --help.\n";return 2;};
    if(args.size()<2 || (args[1]!="run" && args[1]!="smoke"))return invalid();
    QString manifest,output;bool accept=false, allowFixture=false,multiscale=false,plannerSet=false;
    std::vector<std::size_t> accepted;
    for(qsizetype i=2;i<args.size();++i){
        if(args[i]=="--accept-all" && args[1]=="run" && !accept){accept=true;continue;}
        if(args[i]=="--fixture" && args[1]=="run" && !allowFixture){allowFixture=true;continue;}
        if(i+1>=args.size() || args[i+1].startsWith("--"))return invalid();
        if(args[i]=="--output" && output.isEmpty())output=args[++i];
        else if(args[i]=="--planner" && !plannerSet){
            const auto value=args[++i];if(value!="sweep" && value!="multiscale")return invalid();
            multiscale=value=="multiscale";plannerSet=true;
        }
        else if(args[i]=="--manifest" && args[1]=="run" && manifest.isEmpty())manifest=args[++i];
        else if(args[i]=="--accept-index" && args[1]=="run"){
            const auto value=args[++i];bool ok=false;const auto n=value.toULongLong(&ok);
            if(!ok || QString::number(n)!=value || n>std::numeric_limits<std::size_t>::max())return invalid();
            accepted.push_back(std::size_t(n));
        }
        else return invalid();
    }
    if(output.isEmpty() || (args[1]=="run" && (manifest.isEmpty() || (!accept && accepted.empty()) || (accept && !accepted.empty()))))return invalid();
    QTemporaryDir fixture;
    if(!fixture.isValid()){err<<"headless.temporary-directory\n";return 3;}
    auto input=args[1]=="smoke"?smokeFixture(fixture.filePath("source.png")):
        loadSemanticMaskManifest(QUrl::fromLocalFile(manifest),allowFixture);
    if(!input){err<<QString::fromStdString(input.error().messageKey)<<'\n';return 2;}
    if(accept || args[1]=="smoke"){accepted.resize(input.value().names.size());std::iota(accepted.begin(),accepted.end(),0);}
    auto result=runHeadlessWorkflow({std::move(input.value()),std::move(accepted),std::filesystem::path(output.toUtf8().toStdString()),multiscale});
    if(!result){err<<QString::fromStdString(result.error().messageKey)<<'\n';return result.error().code==ErrorCode::Cancelled?4:3;}
    out<<QJsonDocument(result.value().metadata).toJson(QJsonDocument::Compact)<<'\n';return 0;
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{return execute(app.arguments());}
    catch(const std::exception& e){QTextStream(stderr)<<"headless.exception: "<<e.what()<<'\n';return 3;}
}
