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
  // Kartend-fh3ab: the user-state fields the manifest now round-trips.
  // For the three flags "prefer incoming" copies the incoming value
  // EXACTLY (it can clear a local pin); left unchecked, a set flag on
  // either side survives — false is indistinguishable from "never
  // toggled", so the union is the flag counterpart of the string rule
  // "existing wins, but an empty existing is filled from incoming".
  bool preferIncomingNotes = false;
  bool preferIncomingSourceUrl = false;
  bool preferIncomingRating = false;
  bool preferIncomingIsPinned = false;
  bool preferIncomingIsHidden = false;
  bool preferIncomingContinueLater = false;
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

struct ArtworkLinkRestoreResult {
  int written = 0;
  int skipped = 0;
};

/// Kartend-fh3ab: re-create `item_artwork` rows for the hand-linked artwork
/// files a bundle carried (manifest Item::artworkLinks), pointing each link
/// at its extracted payload under @p destDir. A link is skipped — never an
/// error — when its payload is missing (partial extract), its in-bundle path
/// is not a clean relative path (hostile manifest; the extractor never
/// produced it), or the (collectionUuid, path, type) row already carries a
/// local manual link, which an import must not clobber.
[[nodiscard]] ErrorUtils::Result<ArtworkLinkRestoreResult>
persistImportedArtworkLinks(QSqlDatabase &db, const KartManifest::Manifest &manifest,
                            const QString &destDir, const QString &collectionUuid);

} // namespace kart

#endif
