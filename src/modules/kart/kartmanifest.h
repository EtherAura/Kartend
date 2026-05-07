#ifndef KARTMANIFEST_H
#define KARTMANIFEST_H

#include <QByteArray>
#include <QList>
#include <QString>

#include "collectionutils.h"
#include "errorutils.h"
#include "itemmetadata.h"
#include "kartformat.h"

namespace KartManifest {

struct Item {
  QString mediaPath;
  QString artworkPath;
  QString videoPath;
  QString manualPath;
  QString title;
  ItemMetadataStore::ItemMetadata metadata;
  int launcherIndex = -1;

  bool operator==(const Item &other) const = default;
};

struct Manifest {
  quint32 formatVersion = KartFormat::CURRENT_VERSION;
  QString uuid;
  QString version;
  QString createdAt;
  QString name;
  QString author;
  QString description;
  QString license;
  CollectionConfig collectionConfig;
  QList<LauncherPreset> launchers;
  QList<Item> items;

  bool operator==(const Manifest &other) const = default;
};

[[nodiscard]] QByteArray serialize(const Manifest &manifest);

[[nodiscard]] ErrorUtils::Result<Manifest> parse(const QByteArray &json);

} // namespace KartManifest

#endif
