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

/// Walk the container's entry headers (payloads are seeked over, not read)
/// and return how many files an extraction would write — cheap enough for
/// the preflight dialog. The same per-entry bounds extractTo enforces apply
/// here, so a malformed container errors instead of reporting a bogus count.
[[nodiscard]] ErrorUtils::Result<quint32> countEntries(const QString &kartPath);

class Extractor : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(Extractor)

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
