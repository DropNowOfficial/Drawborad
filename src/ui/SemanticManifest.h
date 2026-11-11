#pragma once

#include "segmentation/ProposalEngine.h"
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <vector>

namespace drawborad {
// Validated output of a real text-prompt model runner. No inferred names are
// fabricated by the UI: names and mask hashes come from the model manifest.
struct SemanticManifestImport {
    ProposalSet proposals;
    MaskSet masks;
    QJsonObject sourceReference;
    std::vector<QString> names;
    std::vector<double> scores;
    std::vector<bool> background;
    QString backend;
};
Result<SemanticManifestImport> loadSemanticMaskManifest(const QUrl& manifestUrl, bool allowFixture = false);
} // namespace drawborad
