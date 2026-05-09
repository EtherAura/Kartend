#ifndef KARTWRITER_H
#define KARTWRITER_H

#include <QAtomicInt>
#include <QList>
#include <QObject>
#include <QString>

#include "collectionutils.h"
#include "errorutils.h"
#include "kartformat.h"
#include "kartmanifest.h"

class QSqlDatabase;

namespace KartWriter {

struct ItemSource {
  QString mediaAbs;
  QString artworkAbs;
  QString videoAbs;
  QString manualAbs;
  KartManifest::Item manifestItem;
};

struct WriterParams {
  QString uuid;
  QString version;
  QString name;
  QString author;
  QString description;
  QString license;
  CollectionConfig collectionConfig;
  QList<LauncherPreset> launchers;
  QList<ItemSource> items;
  KartFormat::Compression preferredCompression = KartFormat::Compression_Zstd;
};

[[nodiscard]] bool extensionShouldCompress(const QString &path);

[[nodiscard]] ErrorUtils::Result<WriterParams>
prepareFromCollection(const CollectionConfig &cfg, const QString &collectionUuid,
                      const QList<LauncherPreset> &allPresets, QSqlDatabase *db = nullptr);

class Writer : public QObject {
  Q_OBJECT

public:
  explicit Writer(QObject *parent = nullptr);

  [[nodiscard]] ErrorUtils::Result<void> writeKart(const QString &outPath,
                                                   const WriterParams &params);

  void cancel() { m_cancel.storeRelaxed(1); }

signals:
  void progress(double fraction);

private:
  QAtomicInt m_cancel{0};
};

} // namespace KartWriter

#endif
