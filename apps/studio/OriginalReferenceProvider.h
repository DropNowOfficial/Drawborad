#pragma once
#include "QtBridge.h"
#include <QQuickImageProvider>
namespace drawborad {
// Synchronous GUI-thread provider, immutable cached image; never accesses external paths.
class OriginalReferenceProvider final : public QQuickImageProvider {
 QtBridge& bridge_;
public:
 explicit OriginalReferenceProvider(QtBridge& b):QQuickImageProvider(QQuickImageProvider::Image),bridge_(b){}
 QImage requestImage(const QString& id,QSize* size,const QSize&) override {
  auto image=bridge_.originalReferenceImage(id);if(size)*size=image.size();return image;
 }
};
}
