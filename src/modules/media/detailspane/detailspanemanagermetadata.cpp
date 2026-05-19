// Sibling TU: per-item metadata + gallery resolution and the artwork-links
// editor dialog. Extracted from detailspanemanager.cpp so that file can stay
// focused on layout/visibility concerns. All methods remain DetailsPaneManager
// members and access existing class state via m_*; no behavior change.

#include "detailspanemanager.h"

#include <QDir>

#include "applicationcontext.h"
#include "collectionutils.h"
#include "detailspane.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "itemartwork.h"
#include "itemartworklinksdialog.h"
#include "itemmetadata.h"
#include "itemwidget.h"
#include "pathutils.h"
#include "videoutils.h"

#include "timerutils.h"
#include "uiconstants.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "loggingcategories.h"
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
  // Deselect / clear path runs synchronously: the work is cheap (no DB
  // query, no filesystem probe) and feels wrong to defer — a deselect
  // should visibly clear the sidebar right away. Also drop any queued
  // refresh for a stale selection so it can't fire after the clear.
  if (filePath.isEmpty()) {
    if (m_metadataDebouncer) {
      m_metadataDebouncer->cancel();
    }
    m_pendingMetadataFilePath.clear();
    m_pendingMetadataItemName.clear();
    performSidebarMetadataUpdate(filePath, itemName);
    return;
  }

  // Store the latest target and reset the timer. When several selection
  // changes land within METADATA_DEBOUNCE_MS only the last one's args
  // survive — the heavy DB+FS work runs once at the trailing edge.
  m_pendingMetadataFilePath = filePath;
  m_pendingMetadataItemName = itemName;
  if (m_metadataDebouncer) {
    m_metadataDebouncer->trigger();
  } else {
    // Setup hasn't completed (rare: early tests / shutdown). Fall back
    // to running synchronously so behavior matches the pre-debounce path.
    performSidebarMetadataUpdate(filePath, itemName);
  }
}

void DetailsPaneManager::refreshSidebarMetadataImmediate() {
  // Prefer the pending args over m_currentItem* so a refresh requested
  // while a debounced update is queued sees the new item rather than
  // the previously-displayed one. Both can legitimately be empty (e.g.
  // first call before any selection); the perform function handles that.
  const QString filePath =
      m_pendingMetadataFilePath.isEmpty() ? m_currentItemFilePath : m_pendingMetadataFilePath;
  const QString itemName =
      m_pendingMetadataFilePath.isEmpty() ? m_currentItemName : m_pendingMetadataItemName;
  if (m_metadataDebouncer) {
    m_metadataDebouncer->cancel();
  }
  m_pendingMetadataFilePath.clear();
  m_pendingMetadataItemName.clear();
  performSidebarMetadataUpdate(filePath, itemName);
}

void DetailsPaneManager::performSidebarMetadataUpdate(const QString &filePath,
                                                      const QString &itemName) {
  IDatabaseManager *db = m_ctx ? m_ctx->databaseManager() : nullptr;
  if (!m_DetailsPane || filePath.isEmpty()) {
    if (m_DetailsPane) {
      m_DetailsPane->clearMetadata();
    }
    // drop the published item context so the detail page can't
    // render a stale selection after a deselect / collection switch.
    m_currentItemContext = {};
    return;
  }

  // Perf trace (gated on KARTEND_PERF_TRACE=1) — drives Kartend-5ux9
  // (sidebar refresh hot path). Original audit was DB-only; the dbMs
  // breakdown still lives below for continuity with prior runs, but
  // the live data showed dbMs=0 in 97.7% of cases and the actual cost
  // is in the non-DB phases — so this round adds per-phase timers for
  // each of the FS / pixmap / UI-update operations.
  const bool perfTrace = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE");
  QElapsedTimer perfWall;
  // DB phase timers (existing).
  qint64 perfMetadataMs = 0;
  qint64 perfUsageMs = 0;
  qint64 perfArtworkMs = 0;
  qint64 perfOwnerMs = 0;
  // Non-DB phase timers (new — Kartend-5ux9 breakdown).
  qint64 perfPathExpMs = 0; // PathUtils::validateAndExpandPath x3
  qint64 perfSetMetaMs = 0; // DetailsPane::setMetadata (loads primary cover)
  qint64 perfManualMs = 0;  // ItemMetadataStore::resolveManualFile FS probes
  qint64 perfGalleryMs = 0; // standard + custom + shared artwork FS probes
  qint64 perfVideoMs = 0;   // VideoUtils::findVideoForFile directory scans
  // Per-setter UI timers — split from a previous lumped 'ui' phase after
  // a 3485ms outlier showed UI updates dominate one-off cases. Each
  // setter is a distinct DetailsPane API; lazy widget construction in
  // any one of them would surface here.
  qint64 perfUiExtendedMs = 0; // DetailsPane::setExtendedMetadata
  qint64 perfUiUsageMs = 0;    // DetailsPane::setUsageStats
  qint64 perfUiManualMs = 0;   // DetailsPane::setManualFile
  qint64 perfUiGalleryMs = 0;  // DetailsPane::setArtworkGallery
  if (perfTrace) {
    perfWall.start();
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
  if (db) {
    QElapsedTimer perfOwner;
    if (perfTrace) perfOwner.start();
    owningIndex = db->getCollectionIndexForFile(filePath);
    if (perfTrace) perfOwnerMs = perfOwner.elapsed();
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
  {
    QElapsedTimer perfPath;
    if (perfTrace) perfPath.start();
    if (!artworkDirectory.trimmed().isEmpty()) {
      artworkDirectory = PathUtils::validateAndExpandPath(artworkDirectory, artworkExpansionName);
    }
    if (!videoDirectory.trimmed().isEmpty()) {
      videoDirectory = PathUtils::validateAndExpandPath(videoDirectory, videoExpansionName);
    }
    if (!manualDirectory.trimmed().isEmpty()) {
      manualDirectory = PathUtils::validateAndExpandPath(manualDirectory, manualExpansionName);
    }
    if (perfTrace) perfPathExpMs = perfPath.elapsed();
  }

  debugLog(QString("video lookup: filePath='%1' videoDir='%2' (post-expansion)")
               .arg(filePath, videoDirectory));

  {
    QElapsedTimer perfSm;
    if (perfTrace) perfSm.start();
    m_DetailsPane->setMetadata(filePath, itemName, artworkDirectory, videoDirectory);
    if (perfTrace) perfSetMetaMs = perfSm.elapsed();
  }

  // Extended metadata + manual file.
  ItemMetadataStore::ItemMetadata loadedMetadata;
  if (db && !metaUuid.isEmpty()) {
    QElapsedTimer perfMd;
    if (perfTrace) perfMd.start();
    loadedMetadata = db->loadItemMetadata(metaUuid, filePath);
    if (perfTrace) perfMetadataMs = perfMd.elapsed();
  }
  if (db) {
    QElapsedTimer perfUi;
    if (perfTrace) perfUi.start();
    m_DetailsPane->setExtendedMetadata(loadedMetadata);
    if (perfTrace) perfUiExtendedMs = perfUi.elapsed();
  }

  // Usage statistics. Append play count / last played / time
  // played to the Details section. Loaded after setExtendedMetadata so the
  // section's row layout is already in place; setUsageStats only appends.
  if (db && !metaUuid.isEmpty()) {
    QElapsedTimer perfUsage;
    if (perfTrace) perfUsage.start();
    const auto usage = db->loadItemUsageStats(metaUuid, filePath);
    if (perfTrace) perfUsageMs = perfUsage.elapsed();
    QElapsedTimer perfUi;
    if (perfTrace) perfUi.start();
    m_DetailsPane->setUsageStats(usage);
    if (perfTrace) perfUiUsageMs = perfUi.elapsed();
  }

  const QString baseName = QFileInfo(filePath).completeBaseName();
  // Manual file resolution mirrors the video fallback above: prefer
  // the collection's `manualDirectory` override when set; otherwise
  // look under `{artworkDirectory}/manual/` (where the scraper
  // writes manuals in the single-root layout). Per-item manualPath
  // override still wins over both.
  QString manualPath;
  {
    QElapsedTimer perfMan;
    if (perfTrace) perfMan.start();
    manualPath =
        ItemMetadataStore::resolveManualFile(loadedMetadata.manualPath, baseName, manualDirectory);
    if (manualPath.isEmpty() && loadedMetadata.manualPath.trimmed().isEmpty() &&
        !artworkDirectory.trimmed().isEmpty()) {
      manualPath = ItemMetadataStore::resolveManualFile(QString(), baseName,
                                                        QDir(artworkDirectory).filePath("manual"));
    }
    if (perfTrace) perfManualMs = perfMan.elapsed();
  }
  {
    QElapsedTimer perfUi;
    if (perfTrace) perfUi.start();
    m_DetailsPane->setManualFile(manualPath);
    if (perfTrace) perfUiManualMs = perfUi.elapsed();
  }

  // Build the artwork gallery. For every standard artwork
  // type, prefer the per-item DB override, then fall back to the
  // {artworkDirectory}/{type}/{baseName}.{ext} subdirectory layout. Custom
  // (non-standard) types only resolve via a stored override. An empty list
  // hides the gallery section.
  QList<DetailsPane::GalleryEntry> galleryEntries;
  // Gallery-block timer wraps everything from here through the shared-
  // artwork probes — counts the per-type FS probes (the suspect bulk of
  // non-DB cost). The DB-only loadItemArtwork call inside is excluded
  // because perfArtworkMs already accounts for it separately.
  QElapsedTimer perfGallery;
  if (perfTrace) perfGallery.start();
  if (db && !metaUuid.isEmpty()) {
    QHash<QString, QString> overridesByType;
    QStringList customOrder;
    QElapsedTimer perfArtwork;
    if (perfTrace) perfArtwork.start();
    const auto rows = db->loadItemArtwork(metaUuid, filePath);
    if (perfTrace) perfArtworkMs = perfArtwork.elapsed();
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

    // ── Shared (group/company-scoped) artwork ──────────────────────
    // ScreenScraper exposes group-scoped (`mediaGroup.php`) and
    // company-scoped (`mediaCompagnie.php`) art keyed on the game's
    // groupid + companyid. Persistence routes the bytes to
    // `{artwork}/_shared/<type>/group_<id>.<ext>` (or `company_<id>`)
    // for cross-collection dedup; the scrape parser stamps the ids
    // into customFields here so the gallery probe knows which buckets
    // to scan. Without this, shared files sit on disk invisible.
    //
    // SS-specific type lists kept inline for now — they're a tiny
    // fixed set per the SS docs (group: theme/family backgrounds,
    // figurines, pictoliste; company: publisher/dev wheel + logo).
    // Adding more is one-line append per type.
    if (!artworkDirectory.trimmed().isEmpty() && !loadedMetadata.customFields.isEmpty()) {
      const auto fields = ItemMetadataStore::parseCustomFields(loadedMetadata.customFields);
      QString groupId, companyId;
      for (const auto &kv : fields) {
        if (kv.first == QStringLiteral("screenscraper_groupid"))
          groupId = kv.second;
        else if (kv.first == QStringLiteral("screenscraper_companyid"))
          companyId = kv.second;
      }
      auto probeShared = [&](const QString &type, const QString &scopePrefix,
                             const QString &scopeId, const QString &label) {
        if (scopeId.isEmpty()) return;
        const QString dir = QDir(artworkDirectory).filePath(QStringLiteral("_shared/") + type);
        // Same extension whitelist scrapepersistence.cpp writes; png is
        // the default but the user might have outputformat=jpg or SS
        // might have served webp on a prior scrape.
        for (const char *ext : {"png", "jpg", "jpeg", "webp"}) {
          const QString candidate =
              QDir(dir).filePath(scopePrefix + scopeId + QLatin1Char('.') + QLatin1String(ext));
          if (QFileInfo::exists(candidate)) {
            galleryEntries.append({label, candidate, /*isVideo=*/false});
            return;
          }
        }
      };
      struct SharedType {
        const char *type;
        const char *label;
      };
      static const SharedType kGroupTypes[] = {
          {"background", QT_TR_NOOP("Background (theme)")},
          {"figurine", QT_TR_NOOP("Figurine (theme)")},
          {"pictoliste", QT_TR_NOOP("Picto list (theme)")},
      };
      static const SharedType kCompanyTypes[] = {
          {"company-wheel", QT_TR_NOOP("Wheel (publisher)")},
          {"company-logo-monochrome", QT_TR_NOOP("Logo monochrome (publisher)")},
      };
      for (const auto &t : kGroupTypes) {
        probeShared(QString::fromLatin1(t.type), QStringLiteral("group_"), groupId, tr(t.label));
      }
      for (const auto &t : kCompanyTypes) {
        probeShared(QString::fromLatin1(t.type), QStringLiteral("company_"), companyId,
                    tr(t.label));
      }
    }
  }
  if (perfTrace) {
    // perfGalleryMs covers the FS probes inside the gallery block but
    // EXCLUDES the loadItemArtwork DB call (which is in perfArtworkMs).
    perfGalleryMs = perfGallery.elapsed() - perfArtworkMs;
    if (perfGalleryMs < 0) perfGalleryMs = 0;
  }

  // Prepend the video tile so the gallery follows the video-first
  // ordering the rest of the preview flow uses. Two lookup roots in
  // priority order:
  //   1. The collection's `videoDirectory` (legacy / power-user
  //      override for libraries with videos in a separate folder).
  //   2. `{artworkDirectory}/video/` — the single-root layout that
  //      the scraper writes to. Falls through here when the user
  //      hasn't set `videoDirectory` explicitly.
  QString videoPath;
  {
    QElapsedTimer perfVid;
    if (perfTrace) perfVid.start();
    if (!videoDirectory.trimmed().isEmpty()) {
      videoPath = VideoUtils::findVideoForFile(filePath, videoDirectory);
    }
    if (videoPath.isEmpty() && !artworkDirectory.trimmed().isEmpty()) {
      videoPath = VideoUtils::findVideoForFile(filePath, QDir(artworkDirectory).filePath("video"));
    }
    if (perfTrace) perfVideoMs = perfVid.elapsed();
  }
  if (!videoPath.isEmpty()) {
    galleryEntries.prepend({tr("Video"), videoPath, /*isVideo=*/true});
  }

  {
    QElapsedTimer perfUi;
    if (perfTrace) perfUi.start();
    m_DetailsPane->setArtworkGallery(galleryEntries);
    if (perfTrace) perfUiGalleryMs = perfUi.elapsed();
  }

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

  if (perfTrace) {
    const qint64 totalMs = perfWall.elapsed();
    const qint64 dbMs = perfOwnerMs + perfMetadataMs + perfUsageMs + perfArtworkMs;
    const qint64 nonDbMs = totalMs - dbMs;
    const qint64 uiMs = perfUiExtendedMs + perfUiUsageMs + perfUiManualMs + perfUiGalleryMs;
    // Sum of the phase timers — should approximately equal nonDbMs;
    // the residual is overhead (string ops, struct copies, branches).
    const qint64 phaseSum =
        perfPathExpMs + perfSetMetaMs + perfManualMs + perfGalleryMs + perfVideoMs + uiMs;
    qCDebug(lcPerfTrace).nospace()
        << "performSidebarMetadataUpdate: totalMs=" << totalMs << " dbMs=" << dbMs
        << " (owner=" << perfOwnerMs << " metadata=" << perfMetadataMs << " usage=" << perfUsageMs
        << " artwork=" << perfArtworkMs << ") nonDbMs=" << nonDbMs
        << " phases(setMeta=" << perfSetMetaMs << " gallery=" << perfGalleryMs
        << " video=" << perfVideoMs << " manual=" << perfManualMs << " pathExp=" << perfPathExpMs
        << " ui=" << uiMs << " [extended=" << perfUiExtendedMs << " usageUi=" << perfUiUsageMs
        << " manualUi=" << perfUiManualMs << " galleryUi=" << perfUiGalleryMs
        << "] sum=" << phaseSum << " residual=" << (nonDbMs - phaseSum) << ") path=" << filePath;
  }
}

void DetailsPaneManager::openArtworkLinksDialog() {
  IDatabaseManager *db = m_ctx ? m_ctx->databaseManager() : nullptr;
  if (!m_DetailsPane || !db || !m_collections) {
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
  const auto rows = db->loadItemArtwork(m_currentItemUuid, m_currentItemFilePath);
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
  // Bug: gallery showed types (front/box/etc.) that auto-resolved from
  // {artworkDirectory}/<type>/ but the dialog left those rows blank,
  // making the scrape look only half-mapped. Compute the auto-resolved
  // path for every row that lacks a DB override and pass it through so
  // the dialog can render it as an "auto" hint in the path column.
  if (!m_currentItemArtworkDir.trimmed().isEmpty()) {
    QHash<QString, QString> autoPaths;
    const auto addAuto = [&](const QString &type) {
      if (originalOverrides.contains(type)) {
        return; // override wins, no auto hint needed
      }
      const QString resolved =
          ItemArtworkStore::resolveArtworkPath(QString(), baseName, m_currentItemArtworkDir, type);
      if (!resolved.isEmpty()) {
        autoPaths.insert(type, resolved);
      }
    };
    for (const QString &type : ItemArtworkStore::standardTypes()) {
      addAuto(type);
    }
    for (const QString &type : allCustomTypes) {
      addAuto(type);
    }
    dialog.setAutoResolvedPaths(autoPaths);
  }
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
    db->saveItemArtwork(artwork);
  }
  for (auto it = originalOverrides.constBegin(); it != originalOverrides.constEnd(); ++it) {
    if (visitedTypes.contains(it.key())) {
      continue;
    }
    // Was set, now cleared.
    db->removeItemArtwork(m_currentItemUuid, m_currentItemFilePath, it.key());
  }

  // Refresh the gallery inline using the cached owner context — we don't
  // hold a pointer to the selected ItemWidget here, so we can't re-run
  // updateSidebarMetadata. The logic mirrors that method's gallery build.
  if (m_DetailsPane && !m_currentItemUuid.isEmpty()) {
    QList<DetailsPane::GalleryEntry> galleryEntries;
    QHash<QString, QString> overridesByType;
    QStringList customOrder;
    const auto refreshedRows = db->loadItemArtwork(m_currentItemUuid, m_currentItemFilePath);
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
