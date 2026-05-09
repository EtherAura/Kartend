// Sibling TU: per-item metadata + gallery resolution and the artwork-links
// editor dialog. Extracted from detailspanemanager.cpp so that file can stay
// focused on layout/visibility concerns. All methods remain DetailsPaneManager
// members and access existing class state via m_*; no behavior change.

#include "detailspanemanager.h"

#include "applicationcontext.h"
#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "detailspane.h"
#include "itemartwork.h"
#include "itemartworklinksdialog.h"
#include "itemmetadata.h"
#include "itemwidget.h"
#include "pathutils.h"
#include "videoutils.h"

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcDetailsPaneManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcDetailsPaneManager().isDebugEnabled()) {                                                 \
      qCDebug(lcDetailsPaneManager) << msg;                                                        \
    }                                                                                              \
  } while (0)

void DetailsPaneManager::updateSidebarMetadata(ItemWidget *selectedItem) {
  if (!selectedItem) {
    updateSidebarMetadata(QString{}, QString{});
    return;
  }
  updateSidebarMetadata(selectedItem->getFilePath(), selectedItem->getItemName());
}

void DetailsPaneManager::updateSidebarMetadata(const QString &filePath, const QString &itemName) {
  if (!m_DetailsPane || filePath.isEmpty()) {
    if (m_DetailsPane) {
      m_DetailsPane->clearMetadata();
    }
    // drop the published item context so the detail page can't
    // render a stale selection after a deselect / collection switch.
    m_currentItemContext = {};
    return;
  }

  // Get artwork + video directories from current collection config. Each
  // directory tracks the collection *name* it should be expanded against
  // for %collection% substitution — that name is the current view by
  // default and gets reassigned to the owning collection's name when
  // owner-aware refinement (below) chooses the owner's value.
  QString artworkDirectory;
  QString videoDirectory;
  QString manualDirectory;
  QString collectionName;
  QString artworkExpansionName;
  QString videoExpansionName;
  QString manualExpansionName;
  QString expandedMediaDir;
  if (m_collections && m_currentCollectionIndex >= 0 &&
      m_currentCollectionIndex < m_collections->size()) {
    const CollectionConfig &collection = (*m_collections)[m_currentCollectionIndex];
    artworkDirectory = collection.artworkDirectory;
    videoDirectory = collection.videoDirectory;
    manualDirectory = collection.manualDirectory;
    collectionName = collection.name;
    artworkExpansionName = collection.name;
    videoExpansionName = collection.name;
    manualExpansionName = collection.name;
    expandedMediaDir = PathUtils::validateAndExpandPath(collection.mediaDirectory, collection.name);
  }

  // Resolve the owning collection (may differ from the currently-displayed
  // collection in showAllSubcollectionItems mode) so per-item metadata,
  // manual files, artwork, and video previews all key off the same UUID
  // and inherit from the same directory tree.
  // Hoisted to function scope so the publish-context block below
  // (m_currentItemOwningIndex) can reuse the value without taking the
  // DB mutex a second time for the same path.
  QString metaUuid;
  int owningIndex = -1;
  if (m_databaseManager) {
    owningIndex = m_databaseManager->getCollectionIndexForFile(filePath);
    if (owningIndex >= 0 && m_collections && owningIndex < m_collections->size()) {
      const CollectionConfig &owning = (*m_collections)[owningIndex];
      const QString owningMediaDir =
          PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
      metaUuid = CollectionUtils::computeCollectionUuid(owning.name, owningMediaDir);
      // Prefer the owning collection's manualDirectory in
      // showAllSubcollectionItems mode so a child's directory wins over
      // the parent's when both are set.
      if (!owning.manualDirectory.trimmed().isEmpty()) {
        manualDirectory = owning.manualDirectory;
        manualExpansionName = owning.name;
      } else if (manualDirectory.trimmed().isEmpty() && m_collections) {
        // Fall back to the nearest ancestor with a manualDirectory
        // (mirrors resolveArtworkDirectory's behavior so subcollections
        // inherit). The ancestor's name is unknown to us at this point;
        // %collection% substitution falls back to the owner's name, which
        // is the closest meaningful identifier.
        manualDirectory = CollectionUtils::resolveManualDirectory(owningIndex, *m_collections);
        manualExpansionName = owning.name;
      }
      // Same precedence rules for artworkDirectory so the gallery's
      // subdirectory probe lands in the correct collection's tree.
      if (!owning.artworkDirectory.trimmed().isEmpty()) {
        artworkDirectory = owning.artworkDirectory;
        artworkExpansionName = owning.name;
      } else if (artworkDirectory.trimmed().isEmpty() && m_collections) {
        artworkDirectory = CollectionUtils::resolveArtworkDirectory(owningIndex, *m_collections);
        artworkExpansionName = owning.name;
      }
      // Same for videoDirectory — without this, sidebar video previews
      // miss when a parent aggregates children via
      // showAllSubcollectionItems and only the child has videoDirectory
      // configured. Middle-click + expand-mode already do this by going
      // through the owner's collection directly.
      if (!owning.videoDirectory.trimmed().isEmpty()) {
        videoDirectory = owning.videoDirectory;
        videoExpansionName = owning.name;
      } else if (videoDirectory.trimmed().isEmpty() && m_collections) {
        videoDirectory = CollectionUtils::resolveVideoDirectory(owningIndex, *m_collections);
        videoExpansionName = owning.name;
      }
    } else if (!collectionName.isEmpty()) {
      metaUuid = CollectionUtils::computeCollectionUuid(collectionName, expandedMediaDir);
    }
  }

  // Expand %collection% / ~ in each directory so the lookups use real
  // filesystem paths. validateAndExpandPath returns "" when the resolved
  // directory doesn't exist, which is the right semantics here: the
  // downstream resolvers all guard on emptiness anyway.
  if (!artworkDirectory.trimmed().isEmpty()) {
    artworkDirectory = PathUtils::validateAndExpandPath(artworkDirectory, artworkExpansionName);
  }
  if (!videoDirectory.trimmed().isEmpty()) {
    videoDirectory = PathUtils::validateAndExpandPath(videoDirectory, videoExpansionName);
  }
  if (!manualDirectory.trimmed().isEmpty()) {
    manualDirectory = PathUtils::validateAndExpandPath(manualDirectory, manualExpansionName);
  }

  debugLog(QString("video lookup: filePath='%1' videoDir='%2' (post-expansion)")
               .arg(filePath, videoDirectory));

  m_DetailsPane->setMetadata(filePath, itemName, artworkDirectory, videoDirectory);

  // Extended metadata + manual file.
  ItemMetadataStore::ItemMetadata loadedMetadata;
  if (m_databaseManager && !metaUuid.isEmpty()) {
    loadedMetadata = m_databaseManager->loadItemMetadata(metaUuid, filePath);
  }
  if (m_databaseManager) {
    m_DetailsPane->setExtendedMetadata(loadedMetadata);
  }

  // Usage statistics. Append play count / last played / time
  // played to the Details section. Loaded after setExtendedMetadata so the
  // section's row layout is already in place; setUsageStats only appends.
  if (m_databaseManager && !metaUuid.isEmpty()) {
    const auto usage = m_databaseManager->loadItemUsageStats(metaUuid, filePath);
    m_DetailsPane->setUsageStats(usage);
  }

  const QString baseName = QFileInfo(filePath).completeBaseName();
  const QString manualPath =
      ItemMetadataStore::resolveManualFile(loadedMetadata.manualPath, baseName, manualDirectory);
  m_DetailsPane->setManualFile(manualPath);

  // Build the artwork gallery. For every standard artwork
  // type, prefer the per-item DB override, then fall back to the
  // {artworkDirectory}/{type}/{baseName}.{ext} subdirectory layout. Custom
  // (non-standard) types only resolve via a stored override. An empty list
  // hides the gallery section.
  QList<DetailsPane::GalleryEntry> galleryEntries;
  if (m_databaseManager && !metaUuid.isEmpty()) {
    QHash<QString, QString> overridesByType;
    QStringList customOrder;
    const auto rows = m_databaseManager->loadItemArtwork(metaUuid, filePath);
    for (const auto &row : rows) {
      overridesByType.insert(row.artworkType, row.manualPath);
      if (!ItemArtworkStore::isStandardType(row.artworkType)) {
        customOrder.append(row.artworkType);
      }
    }

    auto pushEntry = [&](const QString &type, const QString &label) {
      const QString resolved = ItemArtworkStore::resolveArtworkPath(
          overridesByType.value(type), baseName, artworkDirectory, type);
      if (!resolved.isEmpty()) {
        galleryEntries.append({label, resolved, /*isVideo=*/false});
      }
    };

    for (const QString &type : ItemArtworkStore::standardTypes()) {
      pushEntry(type, ItemArtworkStore::standardTypeDisplayName(type));
    }
    for (const QString &type : customOrder) {
      // For custom types the user-chosen id IS the human label until (c)
      // adds a per-collection registry of friendly names.
      pushEntry(type, type);
    }
  }

  // Prepend the video tile so the gallery follows the video-first ordering
  // the rest of the preview flow uses. Auto-discovered via
  // VideoUtils against the owning collection's videoDirectory; per-item
  // overrides can be added later alongside the artwork override system.
  if (!videoDirectory.trimmed().isEmpty()) {
    const QString videoPath = VideoUtils::findVideoForFile(filePath, videoDirectory);
    if (!videoPath.isEmpty()) {
      galleryEntries.prepend({tr("Video"), videoPath, /*isVideo=*/true});
    }
  }

  m_DetailsPane->setArtworkGallery(galleryEntries);

  // Capture the resolved owner context so the artwork-link editor dialog
  // doesn't have to redo the showAllSubcollectionItems-aware
  // lookup. Only enable the edit affordance once we have a UUID — without
  // one we couldn't persist anything anyway.
  m_currentItemFilePath = filePath;
  m_currentItemName = itemName;
  m_currentItemUuid = metaUuid;
  m_currentItemArtworkDir = artworkDirectory;
  m_currentItemOwningIndex = owningIndex;
  // publish the resolved owner-aware context so the detail page
  // can render the same item without redoing the lookup. videoDirectory and
  // manualDirectory are already expanded above; capture them all.
  m_currentItemContext.filePath = filePath;
  m_currentItemContext.itemName = itemName;
  m_currentItemContext.uuid = metaUuid;
  m_currentItemContext.artworkDir = artworkDirectory;
  m_currentItemContext.videoDir = videoDirectory;
  m_currentItemContext.manualDir = manualDirectory;
  m_currentItemContext.owningIndex = m_currentItemOwningIndex;
  m_DetailsPane->setArtworkEditEnabled(!metaUuid.isEmpty());
}

void DetailsPaneManager::openArtworkLinksDialog() {
  if (!m_DetailsPane || !m_databaseManager || !m_collections) {
    return;
  }
  if (m_currentItemFilePath.isEmpty() || m_currentItemUuid.isEmpty()) {
    return;
  }

  // Resolve the custom-types list from the owning collection (which can
  // differ from the currently-displayed collection in
  // showAllSubcollectionItems mode). Falls back to an empty list if the
  // owning index has been invalidated mid-flight.
  QStringList customTypes;
  if (m_currentItemOwningIndex >= 0 && m_currentItemOwningIndex < m_collections->size()) {
    customTypes = (*m_collections)[m_currentItemOwningIndex].customArtworkTypes;
  }

  const QString baseName = QFileInfo(m_currentItemFilePath).completeBaseName();

  // Snapshot the current overrides so we can compute insert/update/delete
  // diffs after the dialog is accepted. Also include any custom-type rows
  // already stored in the DB but no longer listed in the collection's
  // config — that way the user can clear stale entries instead of being
  // unable to see them. We render those as extra "custom" rows.
  QHash<QString, QString> originalOverrides;
  QStringList allCustomTypes = customTypes;
  const auto rows = m_databaseManager->loadItemArtwork(m_currentItemUuid, m_currentItemFilePath);
  for (const auto &row : rows) {
    originalOverrides.insert(row.artworkType, row.manualPath);
    if (!ItemArtworkStore::isStandardType(row.artworkType) &&
        !allCustomTypes.contains(row.artworkType)) {
      allCustomTypes.append(row.artworkType);
    }
  }

  ItemArtworkLinksDialog dialog(m_DetailsPane->window());
  dialog.setItemTitle(m_currentItemName.isEmpty() ? baseName : m_currentItemName);
  dialog.setTypeRows(ItemArtworkStore::standardTypes(), allCustomTypes);
  dialog.setOverrides(originalOverrides);
  if (!m_currentItemArtworkDir.trimmed().isEmpty()) {
    dialog.setBrowseStartDirectory(m_currentItemArtworkDir);
  }

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const QHash<QString, QString> newOverrides = dialog.overrides();

  // Persist the diff: every type whose final value differs from the
  // original gets either a save (non-empty) or a remove (cleared). We
  // intentionally do NOT batch this in a transaction — the existing
  // ItemArtworkStore API is single-row, and a few extra round-trips per
  // edit session is negligible compared to the UI feedback latency.
  QSet<QString> visitedTypes;
  for (auto it = newOverrides.constBegin(); it != newOverrides.constEnd(); ++it) {
    visitedTypes.insert(it.key());
    const QString original = originalOverrides.value(it.key());
    if (it.value() == original) {
      continue;
    }
    ItemArtworkStore::ItemArtwork artwork;
    artwork.collectionUuid = m_currentItemUuid;
    artwork.path = m_currentItemFilePath;
    artwork.artworkType = it.key();
    artwork.manualPath = it.value();
    m_databaseManager->saveItemArtwork(artwork);
  }
  for (auto it = originalOverrides.constBegin(); it != originalOverrides.constEnd(); ++it) {
    if (visitedTypes.contains(it.key())) {
      continue;
    }
    // Was set, now cleared.
    m_databaseManager->removeItemArtwork(m_currentItemUuid, m_currentItemFilePath, it.key());
  }

  // Refresh the gallery inline using the cached owner context — we don't
  // hold a pointer to the selected ItemWidget here, so we can't re-run
  // updateSidebarMetadata. The logic mirrors that method's gallery build.
  if (m_DetailsPane && !m_currentItemUuid.isEmpty()) {
    QList<DetailsPane::GalleryEntry> galleryEntries;
    QHash<QString, QString> overridesByType;
    QStringList customOrder;
    const auto refreshedRows =
        m_databaseManager->loadItemArtwork(m_currentItemUuid, m_currentItemFilePath);
    for (const auto &row : refreshedRows) {
      overridesByType.insert(row.artworkType, row.manualPath);
      if (!ItemArtworkStore::isStandardType(row.artworkType)) {
        customOrder.append(row.artworkType);
      }
    }
    auto pushEntry = [&](const QString &type, const QString &label) {
      const QString resolved = ItemArtworkStore::resolveArtworkPath(
          overridesByType.value(type), baseName, m_currentItemArtworkDir, type);
      if (!resolved.isEmpty()) {
        galleryEntries.append({label, resolved, /*isVideo=*/false});
      }
    };
    for (const QString &type : ItemArtworkStore::standardTypes()) {
      pushEntry(type, ItemArtworkStore::standardTypeDisplayName(type));
    }
    for (const QString &type : customOrder) {
      pushEntry(type, type);
    }
    m_DetailsPane->setArtworkGallery(galleryEntries);
  }
}
