#ifndef KARTREADER_H
#define KARTREADER_H

#include <QAtomicInt>
#include <QList>
#include <QObject>
#include <QString>

#include "errorutils.h"
#include "kartmanifest.h"

namespace KartReader {

struct ExtractedFile {
  QString relPath;
  QString absPath;
  quint8 flags = 0;
};

struct ExtractResult {
  KartManifest::Manifest manifest;
  QString destDir;
  QList<ExtractedFile> files;
};

[[nodiscard]] ErrorUtils::Result<KartManifest::Manifest> peekManifest(const QString &kartPath);

class Extractor : public QObject {
  Q_OBJECT

public:
  explicit Extractor(QObject *parent = nullptr);

  [[nodiscard]] ErrorUtils::Result<ExtractResult> extractTo(const QString &kartPath,
                                                            const QString &destDir);

  void cancel() { m_cancel.storeRelaxed(1); }

signals:
  void progress(double fraction);
  void entryExtracted(const QString &relPath);

private:
  QAtomicInt m_cancel{0};
};

} // namespace KartReader

#endif
