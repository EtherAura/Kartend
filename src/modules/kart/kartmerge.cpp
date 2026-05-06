#include "kartmerge.h"

#include <QDir>
#include <QSqlDatabase>

namespace kart {

namespace {

QString pickString(const QString &existing, const QString &incoming, bool preferIncoming) {
  if (preferIncoming) {
    return incoming.isEmpty() ? existing : incoming;
  }
  return existing.isEmpty() ? incoming : existing;
}

int pickRuntime(int existing, int incoming, bool preferIncoming) {
  if (preferIncoming) {
    return incoming >= 0 ? incoming : existing;
  }
  return existing >= 0 ? existing : incoming;
}

int pickLauncher(int existing, int incoming, bool preferIncoming) {
  if (preferIncoming) {
    return incoming >= 0 ? incoming : existing;
  }
  return existing >= 0 ? existing : incoming;
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
  out.runtimeSeconds =
      pickRuntime(existing.runtimeSeconds, incoming.runtimeSeconds, p.preferIncomingRuntimeSeconds);
  out.tags = pickString(existing.tags, incoming.tags, p.preferIncomingTags);
  out.customFields =
      pickString(existing.customFields, incoming.customFields, p.preferIncomingCustomFields);
  out.manualPath = pickString(existing.manualPath, incoming.manualPath, p.preferIncomingManualPath);
  out.launcherIndex =
      pickLauncher(existing.launcherIndex, incoming.launcherIndex, p.preferIncomingLauncherIndex);
  out.source = pickString(existing.source, incoming.source, p.preferIncomingSource);
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

} // namespace kart
