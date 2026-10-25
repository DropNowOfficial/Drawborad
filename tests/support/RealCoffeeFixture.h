#pragma once
#include "core/Digest.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>
namespace drawborad::fixture {
// Lossless recording of actual native model outputs for fast reconstruction
// regressions. Live model inference remains independently tested in M03/U04.
inline Bytes readCoffeeMask(const QString& path){
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly) || file.size()>1024*1024)throw std::runtime_error("missing real native coffee mask fixture");
    const auto object=QJsonDocument::fromJson(file.readAll()).object();
    if(object.value("width").toInteger()!=600 || object.value("height").toInteger()!=400)throw std::runtime_error("coffee mask dimensions");
    auto value=object.value("first_value").toInteger(-1);
    if(value!=0 && value!=255)throw std::runtime_error("coffee mask initial value");
    Bytes bytes;bytes.reserve(240000);
    for(const auto& run:object.value("runs").toArray()){
        const auto count=run.toInteger(-1);
        if(count<=0 || count>240000 || std::size_t(count)>240000-bytes.size())throw std::runtime_error("coffee mask run bounds");
        bytes.insert(bytes.end(),std::size_t(count),std::uint8_t(value));value=255-value;
    }
    if(bytes.size()!=240000 || sha256(bytes).hex()!=object.value("sha256").toString().toStdString())throw std::runtime_error("coffee mask hash");
    return bytes;
}
inline Bytes realCoffeeMask(unsigned index){
    return readCoffeeMask(QDir(qEnvironmentVariable("DRAWBORAD_COFFEE_MASKS")).filePath(QString("auto-mask%1.json").arg(index)));
}
#ifdef DRAWBORAD_SOURCE_DIR
inline Bytes reviewedCoffeeMask(unsigned index){
    return readCoffeeMask(QStringLiteral(DRAWBORAD_SOURCE_DIR "/tests/fixtures/coffee-reviewed-scene/mask%1.json").arg(index));
}
#endif
}
