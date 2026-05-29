#ifndef KARTMANIFEST_H
#define KARTMANIFEST_H

#include <QByteArray>
#include <QList>
#include <QString>

#include "collection/collectionconfig.h"
#include "collection/launcherpreset.h"
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

/// One item entry inside a bundled playlist. The reference is keyed by the
/// in-bundle `media_path` (relative to the kart root) because that's the
/// stable handle across import/export — `PlaylistItemRef`'s
/// (sourceCollectionUuid, sourcePath) wire form is resolved on import to
/// the new collection's UUID + finalized absolute path.
struct PlaylistItemEntry {
  QString mediaPath;
  int position = -1;

  bool operator==(const PlaylistItemEntry &other) const = default;
};

/// A complete playlist row. Both static and smart playlists round-trip
/// through the same struct because the serialization difference is just
/// `is_smart` + `smart_filter` payload. `items` is empty for smart
/// playlists (their content is derived from the filter at view time).
struct PlaylistEntry {
  QString name;
  QString icon;
  QString reservedKind;
  bool isSmart = false;
  QString smartFilterJson;
  QList<PlaylistItemEntry> items;

  bool operator==(const PlaylistEntry &other) const = default;
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
  /// Bundled playlists (Kartend-kmj1). Empty for v1 bundles and for any
  /// collection that had no playlists at export time. Smart playlists
  /// keep their filter JSON; static playlists carry resolved item
  /// references keyed by the bundled `media_path`.
  QList<PlaylistEntry> playlists;

  bool operator==(const Manifest &other) const = default;
};

[[nodiscard]] QByteArray serialize(const Manifest &manifest);

[[nodiscard]] ErrorUtils::Result<Manifest> parse(const QByteArray &json);

} // namespace KartManifest

#endif
