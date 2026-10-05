#include "Test.h"
#include "apps/studio/ApplicationController.h"
#include "platform/WinDiagnostics.h"
#include <chrono>
#include <filesystem>
using namespace drawborad;
TEST("controller empty state is genuinely empty") {
    ApplicationController c;
    CHECK(c.state() == ApplicationState::Empty);
    CHECK(!c.snapshot());
    CHECK(!c.dirty());
    NewDocumentRequest r;
    r.dimensions = {3173, 911, Unit::Px, 300};
    CHECK(c.newDocument(r));
    CHECK(c.snapshot()->spec.pixelSize == PixelSize{3173, 911});
    CHECK(c.snapshot()->eventIndex->empty());
    CHECK(c.snapshot()->nodes->size() == 1);
}
TEST("open failure retains the existing document and exposes error state") {
    ApplicationController c;
    NewDocumentRequest r;
    r.dimensions = {10, 20, Unit::Px, 300};
    CHECK(c.newDocument(r));
    auto before = *c.snapshot();
    CHECK(!c.open(std::filesystem::temp_directory_path() / "does-not-exist-drawborad" /
                  "missing.draw"));
    CHECK(c.state() == ApplicationState::Error);
    CHECK(c.snapshot()->sameContent(before));
    CHECK(!c.message().empty());
}
TEST("controller new save reopen and revision commands") {
    auto dir = std::filesystem::temp_directory_path() /
               ("drawborad-controller-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
        }
    } cleanup{dir};
    ApplicationController c;
    NewDocumentRequest r;
    r.dimensions = {512, 512, Unit::Px, 300};
    CHECK(c.newDocument(r));
    CHECK(c.apply(Command{RenameNode{Document::rootId(), "project"}}));
    CHECK(c.dirty());
    CHECK(c.undo());
    CHECK(c.redo());
    auto before = *c.snapshot();
    CHECK(c.save(dir / "test.draw"));
    CHECK(!c.dirty());
    ApplicationController b;
    CHECK(b.open(dir / "test.draw"));
    CHECK(b.snapshot()->sameContent(before));
    CHECK(b.state() == ApplicationState::Ready);
}
TEST("diagnostics are measured and do not invent an active render GPU") {
    auto report = collectDeviceReport();
    CHECK(!report.osBuild.empty());
    CHECK(!report.cpu.empty());
    CHECK(report.ram > 0);
    CHECK(!report.activeRenderLuid);
    CHECK(report.sceneGraph == "NotInitialized");
    CHECK(report.moduleScanCompleted);
    CHECK(report.webviewModules.empty());
    auto json = diagnosticJson(report);
    CHECK(json.find("\"activeRenderLuid\":null") != std::string::npos);
    CHECK(json.find("\"osBuild\"") != std::string::npos);
}
