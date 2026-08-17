#ifndef KARTWRITER_H
#define KARTWRITER_H

#include <QAtomicInt>
#include <QList>
#include <QObject>
#include <QString>

#include "collection/collectionconfig.h"
#include "collection/launcherpreset.h"
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
  /// Kartend-fh3ab: absolute source path of each bundled hand-linked artwork
  /// file, index-aligned with manifestItem.artworkLinks (whose .path is the
  /// in-bundle destination the writer copies this file to).
  QStringList artworkLinkAbs;
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
  /// Bundled playlists (Kartend-kmj1). Empty unless the caller fetched
  /// playlists from PlaylistManager for the exported collection's UUID.
  /// The writer copies these straight onto the manifest; resolution of
  /// PlaylistItemRefs to in-bundle relative paths happens at the call
  /// site so KartWriter stays free of PlaylistManager linkage.
  QList<KartManifest::PlaylistEntry> playlists;
  KartFormat::Compression preferredCompression = KartFormat::Compression_Zstd;
};

[[nodiscard]] bool extensionShouldCompress(const QString &path);

[[nodiscard]] ErrorUtils::Result<WriterParams>
prepareFromCollection(const CollectionConfig &cfg, const QString &collectionUuid,
                      const QList<LauncherPreset> &allPresets, QSqlDatabase *db = nullptr);

class Writer : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(Writer)

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
