#include "kartmerge.h"

#include <QDir>
#include <QFile>
#include <QSqlDatabase>

#include "itemartwork.h"

namespace kart {

namespace {

QString pickString(const QString &existing, const QString &incoming, bool preferIncoming) {
  if (preferIncoming) {
    return incoming.isEmpty() ? existing : incoming;
  }
  return existing.isEmpty() ? incoming : existing;
}

// Negative means "unset" for every optional int column (runtime, launcher
// override, rating) — one picker serves all three.
int pickOptionalInt(int existing, int incoming, bool preferIncoming) {
  if (preferIncoming) {
    return incoming >= 0 ? incoming : existing;
  }
  return existing >= 0 ? existing : incoming;
}

// Kartend-fh3ab: false is indistinguishable from "never toggled", so the
// default arm unions the sides — the flag counterpart of pickString's
// existing-wins-but-empty-fills rule. "Prefer incoming" copies the incoming
// value exactly, which is deliberately able to clear a local flag.
bool pickFlag(bool existing, bool incoming, bool preferIncoming) {
  return preferIncoming ? incoming : (existing || incoming);
}

} // namespace

ItemMetadataStore::ItemMetadata mergeItemMetadata(const ItemMetadataStore::ItemMetadata &existing,
                                                  const ItemMetadataStore::ItemMetadata &incoming,
                                                  const MergePolicy &p) {
  ItemMetadataStore::ItemMetadata out;
  out.collectionUuid =
      existing.collectionUuid.isEmpty() ? incoming.collectionUuid : existing.collectionUuid;
  out.path = existing.path.isEmpty() ? incoming.path : existing.path;
  out.title = pickString(existing.title, incoming.title, p.preferIncomingTitle);
  out.description =
      pickString(existing.description, incoming.description, p.preferIncomingDescription);
  out.genre = pickString(existing.genre, incoming.genre, p.preferIncomingGenre);
  out.developer = pickString(existing.developer, incoming.developer, p.preferIncomingDeveloper);
  out.publisher = pickString(existing.publisher, incoming.publisher, p.preferIncomingPublisher);
  out.releaseDate =
      pickString(existing.releaseDate, incoming.releaseDate, p.preferIncomingReleaseDate);
  out.contentRating =
      pickString(existing.contentRating, incoming.contentRating, p.preferIncomingContentRating);
  out.players = pickString(existing.players, incoming.players, p.preferIncomingPlayers);
  out.runtimeSeconds = pickOptionalInt(existing.runtimeSeconds, incoming.runtimeSeconds,
                                       p.preferIncomingRuntimeSeconds);
  out.tags = pickString(existing.tags, incoming.tags, p.preferIncomingTags);
  out.customFields =
      pickString(existing.customFields, incoming.customFields, p.preferIncomingCustomFields);
  out.manualPath = pickString(existing.manualPath, incoming.manualPath, p.preferIncomingManualPath);
  out.launcherIndex = pickOptionalInt(existing.launcherIndex, incoming.launcherIndex,
                                      p.preferIncomingLauncherIndex);
  out.source = pickString(existing.source, incoming.source, p.preferIncomingSource);
  out.notes = pickString(existing.notes, incoming.notes, p.preferIncomingNotes);
  out.sourceUrl = pickString(existing.sourceUrl, incoming.sourceUrl, p.preferIncomingSourceUrl);
  out.rating = pickOptionalInt(existing.rating, incoming.rating, p.preferIncomingRating);
  out.isPinned = pickFlag(existing.isPinned, incoming.isPinned, p.preferIncomingIsPinned);
  out.isHidden = pickFlag(existing.isHidden, incoming.isHidden, p.preferIncomingIsHidden);
  out.continueLater =
      pickFlag(existing.continueLater, incoming.continueLater, p.preferIncomingContinueLater);
  out.updatedAt = existing.updatedAt;
  return out;
}

ErrorUtils::Result<PersistResult> persistImportedMetadata(QSqlDatabase &db,
                                                          const KartManifest::Manifest &manifest,
                                                          const QString &destDir,
                                                          const QString &collectionUuid,
                                                          const ConflictResolver &resolver) {
  if (!db.isOpen()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::DatabaseNotOpen,
                                           "Database not open for metadata persistence",
                                           "kart::persistImportedMetadata");
  }

  PersistResult result;
  bool bulkActive = false;
  ConflictResolution bulkResolution;

  for (const KartManifest::Item &item : manifest.items) {
    if (item.mediaPath.isEmpty()) continue;

    const QString resolvedPath = QDir(destDir).filePath(item.mediaPath);

    ItemMetadataStore::ItemMetadata incoming = item.metadata;
    incoming.collectionUuid = collectionUuid;
    incoming.path = resolvedPath;
    if (incoming.title.isEmpty() && !item.title.isEmpty()) {
      incoming.title = item.title;
    }
    if (incoming.launcherIndex < 0 && item.launcherIndex >= 0) {
      incoming.launcherIndex = item.launcherIndex;
    }

    if (incoming.isEmpty()) {
      ++result.skipped;
      continue;
    }

    auto existingRes = ItemMetadataStore::load(db, collectionUuid, resolvedPath);
    if (existingRes.isError()) {
      return existingRes.error();
    }
    const ItemMetadataStore::ItemMetadata &existing = existingRes.value();

    if (existing.isEmpty()) {
      auto save = ItemMetadataStore::save(db, incoming);
      if (save.isError()) return save.error();
      ++result.written;
      continue;
    }

    ConflictResolution resolution;
    if (bulkActive) {
      resolution = bulkResolution;
    } else if (resolver) {
      resolution = resolver(resolvedPath, existing, incoming);
      if (resolution.applyToAll) {
        bulkActive = true;
        bulkResolution = resolution;
      }
    } else {
      resolution.choice = MergeChoice::Skip;
    }

    switch (resolution.choice) {
    case MergeChoice::Skip:
      ++result.skipped;
      break;
    case MergeChoice::Overwrite: {
      auto save = ItemMetadataStore::save(db, incoming);
      if (save.isError()) return save.error();
      ++result.overwritten;
      break;
    }
    case MergeChoice::Merge: {
      ItemMetadataStore::ItemMetadata merged =
          mergeItemMetadata(existing, incoming, resolution.policy);
      auto save = ItemMetadataStore::save(db, merged);
      if (save.isError()) return save.error();
      ++result.merged;
      break;
    }
    }
  }

  return result;
}

ErrorUtils::Result<ArtworkLinkRestoreResult>
persistImportedArtworkLinks(QSqlDatabase &db, const KartManifest::Manifest &manifest,
                            const QString &destDir, const QString &collectionUuid) {
  if (!db.isOpen()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::DatabaseNotOpen,
                                           "Database not open for artwork link persistence",
                                           "kart::persistImportedArtworkLinks");
  }

  ArtworkLinkRestoreResult result;
  for (const KartManifest::Item &item : manifest.items) {
    if (item.mediaPath.isEmpty() || item.artworkLinks.isEmpty()) continue;
    const QString resolvedItemPath = QDir(destDir).filePath(item.mediaPath);

    for (const KartManifest::ArtworkLink &link : item.artworkLinks) {
      // The extractor only ever writes clean relative paths, so a manifest
      // link that is absolute or climbs out of the bundle root was authored
      // by hand — never follow it (a link row must not point outside the
      // imported tree it claims to describe).
      const QString cleanedRel = QDir::cleanPath(link.path);
      if (link.type.isEmpty() || cleanedRel.isEmpty() || QDir::isAbsolutePath(cleanedRel) ||
          cleanedRel == QLatin1String("..") || cleanedRel.startsWith(QLatin1String("../"))) {
        ++result.skipped;
        continue;
      }
      const QString target = QDir(destDir).filePath(cleanedRel);
      if (!QFile::exists(target)) {
        ++result.skipped; // partial extract — the import already surfaced it
        continue;
      }
      auto existing = ItemArtworkStore::load(db, collectionUuid, resolvedItemPath, link.type);
      if (existing.isError()) return existing.error();
      if (!existing.value().manualPath.isEmpty()) {
        ++result.skipped; // a local hand link outranks the bundle's copy
        continue;
      }
      ItemArtworkStore::ItemArtwork row;
      row.collectionUuid = collectionUuid;
      row.path = resolvedItemPath;
      row.artworkType = link.type;
      row.manualPath = target;
      auto save = ItemArtworkStore::save(db, row);
      if (save.isError()) return save.error();
      ++result.written;
    }
  }
  return result;
}

} // namespace kart
