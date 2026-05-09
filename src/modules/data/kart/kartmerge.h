#ifndef KARTMERGE_H
#define KARTMERGE_H

#include <functional>

#include "errorutils.h"
#include "itemmetadata.h"
#include "kartmanifest.h"

class QSqlDatabase;

namespace kart {

enum class MergeChoice { Skip, Overwrite, Merge };

struct MergePolicy {
  bool preferIncomingTitle = false;
  bool preferIncomingDescription = false;
  bool preferIncomingGenre = false;
  bool preferIncomingDeveloper = false;
  bool preferIncomingPublisher = false;
  bool preferIncomingReleaseDate = false;
  bool preferIncomingContentRating = false;
  bool preferIncomingPlayers = false;
  bool preferIncomingRuntimeSeconds = false;
  bool preferIncomingTags = false;
  bool preferIncomingCustomFields = false;
  bool preferIncomingManualPath = false;
  bool preferIncomingLauncherIndex = false;
  bool preferIncomingSource = false;
};

[[nodiscard]] ItemMetadataStore::ItemMetadata
mergeItemMetadata(const ItemMetadataStore::ItemMetadata &existing,
                  const ItemMetadataStore::ItemMetadata &incoming, const MergePolicy &policy);

struct ConflictResolution {
  MergeChoice choice = MergeChoice::Skip;
  MergePolicy policy;
  bool applyToAll = false;
};

using ConflictResolver = std::function<ConflictResolution(
    const QString &itemPath, const ItemMetadataStore::ItemMetadata &existing,
    const ItemMetadataStore::ItemMetadata &incoming)>;

struct PersistResult {
  int written = 0;
  int skipped = 0;
  int merged = 0;
  int overwritten = 0;
};

[[nodiscard]] ErrorUtils::Result<PersistResult>
persistImportedMetadata(QSqlDatabase &db, const KartManifest::Manifest &manifest,
                        const QString &destDir, const QString &collectionUuid,
                        const ConflictResolver &resolver);

} // namespace kart

#endif
