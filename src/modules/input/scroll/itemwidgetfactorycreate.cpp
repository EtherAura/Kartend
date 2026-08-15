// Widget creation + media path resolution + artwork configuration cluster
// split out from itemwidgetfactory.cpp.
#include "collection/hierarchyhelpers.h"
#include "collection/typehelpers.h"
#include "itemwidgetfactory.h"

#include "artworkutils.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "itemartwork.h"
#include "itemwidget.h"
#include "loggingcategories.h"
#include "pathutils.h"
#include "widgetpoolmanager.h"

#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QtGlobal>

ItemWidget *ItemWidgetFactory::createMediaWidget(int mediaIndex, int &collectionIndex) {
  if (!m_filePaths || mediaIndex < 0 || mediaIndex >= m_filePaths->size()) {
    return nullptr;
  }

  const QString rawFileName = m_filePaths->at(mediaIndex);
  if (rawFileName.isEmpty()) {
    // Request items from database and return placeholder
    const int chunkSize = computeChunkSize();
    int chunkStart = (mediaIndex / chunkSize) * chunkSize;
    // Only emit request if this chunk isn't already pending
    if (!m_pendingRangeRequests.contains(chunkStart)) {
      m_pendingRangeRequests.insert(chunkStart);
      qCDebug(lcSearchDiag) << "[ItemWidgetFactory] requestItemsRange: mediaIndex=" << mediaIndex
                            << "chunkStart=" << chunkStart << "chunkSize=" << chunkSize;
      emit requestItemsRange(chunkStart, chunkSize);

      // Prefetch adjacent chunks to reduce perceived latency during scrolling
      prefetchAdjacentChunks(mediaIndex, chunkSize);
    }
    return createPlaceholderWidget();
  }

  QString fullPath;
  QString displayName;
  collectionIndex = m_context.currentIndex;

  resolveMediaItemPaths(rawFileName, fullPath, displayName, collectionIndex);
  if (fullPath.isEmpty()) {
    return nullptr;
  }

  auto *widget = acquireWidget();
  if (!widget) {
    return nullptr;
  }
  configureBaseWidget(widget);
  widget->setFilePath(fullPath);
  widget->setItemName(displayName);

  // Set collection name for list mode display
  if (m_context.config.viewType == ViewType::List && m_collections && collectionIndex >= 0 &&
      collectionIndex < m_collections->size()) {
    widget->setCollectionName(m_collections->at(collectionIndex).name);
  }

  // Skip artwork loading in list mode - but check if artwork exists for the
  // icon
  const QString placeholderArtwork = resolvePlaceholderArtworkForCollection(collectionIndex);
  if (m_context.config.viewType != ViewType::List) {
    configureArtworkForWidget(widget, fullPath);
  } else {
    // In list mode, check if artwork exists to show the preview button
    // Need to look up artwork from the item's collection, not the current
    // collection
    QString artworkDir;
    if (m_collections && collectionIndex >= 0 && collectionIndex < m_collections->size()) {
      artworkDir = m_collections->at(collectionIndex).artworkDirectory;
    }
    if (artworkDir.isEmpty()) {
      artworkDir = m_context.config.artworkDirectory;
    }
    // Store artwork directory for preview overlay to use
    widget->setArtworkDirectory(artworkDir);
    // A hand-linked cover counts here exactly as it does on the tile
    // (Kartend-ni68u). It is checked BEFORE the name cascade and before the
    // artworkDir guard: a link makes an item count even when its collection
    // has no artwork directory at all, which is the case the cascade below
    // cannot see. ScrollManager::showArtworkPreview routes the button to the
    // linked path, so the preview this flag promises is the one that opens.
    const QString manualCover =
        m_manualCoverPaths.isEmpty() ? QString() : m_manualCoverPaths.value(fullPath);
    if (!manualCover.isEmpty()) {
      widget->setHasArtwork(true);
    } else if (!artworkDir.isEmpty()) {
      QString fileName = QFileInfo(fullPath).fileName();
      QString artworkPath = ArtworkUtils::findArtworkForFileCached(fileName, artworkDir);
      // Cold-cache fallback: findArtworkForFileCached returns empty on the
      // first lookup for an uncached directory (it queues a background
      // scan). The synchronous fallback paints the artwork preview button
      // on the very first frame, but during a fast scroll across hundreds
      // of items in an uncached directory it serialises a filesystem stat
      // per widget on the GUI thread.
      //
      // Gate the sync fallback on whether a prewarm is already in flight:
      // if a background thread is about to populate the cache, skip the
      // per-widget stat and let ScrollManager's post-prewarm
      // reconfigureArtworkForActiveWidgets() set hasArtwork once the cache
      // is warm (typically tens of ms later). Outside the scroll-burst
      // case the prewarm queue is empty, so the sync fallback still fires
      // and the button still appears on first paint as before.
      if (artworkPath.isEmpty() &&
          !ArtworkUtils::DirectoryCache::instance().isDirectoryQueued(artworkDir)) {
        artworkPath = ArtworkUtils::findArtworkForFile(fileName, artworkDir);
      }
      widget->setHasArtwork(!artworkPath.isEmpty());
    }
  }
  applyPlaceholderArtwork(widget, placeholderArtwork);

  return widget;
}

ItemWidget *ItemWidgetFactory::createPlaceholderWidget() {
  auto *widget = acquireWidget();
  if (!widget) {
    return nullptr;
  }
  configureBaseWidget(widget);
  if (widget->nameLabel) {
    widget->nameLabel->setText("Loading...");
  }
  return widget;
}

void ItemWidgetFactory::resolveMediaItemPaths(const QString &rawFileName, QString &fullPath,
                                              QString &displayName, int &collectionIndex) {
  auto *db = dbMgr();
  if (!db) {
    return;
  }

  // Use DatabaseManager for path resolution
  fullPath = db->resolveFilePath(rawFileName, m_context);

  if (!fullPath.isEmpty()) {
    if (m_context.config.showAllSubcollectionItems) {
      displayName =
          m_fileNames
              ? m_fileNames->value(
                    fullPath, QFileInfo(fullPath).completeBaseName().replace('_', ' ').simplified())
              : QFileInfo(fullPath).completeBaseName().replace('_', ' ').simplified();
    } else {
      displayName = m_fileNames
                        ? m_fileNames->value(fullPath, QFileInfo(rawFileName).completeBaseName())
                        : QFileInfo(rawFileName).completeBaseName();
    }

    // When searching with virtual folders, prepend the subfolder path to help
    // identify which folder the result came from. This applies when:
    // - Collection uses virtual folders (includeContentSubfolders &&
    // !showAllSubfolderItems)
    // - Search is active (suppressVirtualFolders is set during search)
    if (m_context.suppressVirtualFolders &&
        m_context.config.folderBrowsing.includeContentSubfolders &&
        !m_context.config.folderBrowsing.showAllSubfolderItems) {
      const QString &mediaDir = m_context.config.mediaDirectory;
      if (!mediaDir.isEmpty()) {
        QDir mediaDirObj(mediaDir);
        QString relativePath = mediaDirObj.relativeFilePath(fullPath);
        QString relativeDir = QFileInfo(relativePath).path();
        if (!relativeDir.isEmpty() && relativeDir != QStringLiteral(".")) {
          displayName = relativeDir + QStringLiteral("/") + displayName;
        }
      }
    }

    updateCollectionIndexFromDatabase(fullPath, collectionIndex);
  }
}

void ItemWidgetFactory::updateCollectionIndexFromDatabase(const QString &fullPath,
                                                          int &collectionIndex) {
  if (auto *db = dbMgr()) {
    int detectedCollectionIndex = db->getCollectionIndexForFile(fullPath);
    if (detectedCollectionIndex >= 0) {
      collectionIndex = detectedCollectionIndex;
    }
  }
}

QString ItemWidgetFactoryHelpers::resolvePlaceholderArtwork(
    const QList<CollectionConfig> *collections, int collectionIndex,
    const QString &contextPlaceholder, const QString &contextCollectionName) {
  const bool validIndex =
      collections && collectionIndex >= 0 && collectionIndex < collections->size();
  QString placeholderArtwork;
  if (validIndex) {
    placeholderArtwork =
        CollectionUtils::resolvePlaceholderArtwork(collectionIndex, *collections).trimmed();
  }
  if (placeholderArtwork.isEmpty()) {
    placeholderArtwork = contextPlaceholder.trimmed();
  }
  if (!placeholderArtwork.isEmpty()) {
    const QString collectionName =
        validIndex ? collections->at(collectionIndex).name : contextCollectionName;
    // File-shaped validation (Kartend-80h8o): expandConfigVariables gates on
    // QDir::exists, which is false for every FILE, so it resolved a real
    // placeholder image to empty and the feature silently no-opped.
    placeholderArtwork = PathUtils::validateAndExpandFilePath(placeholderArtwork, collectionName);
  }
  return placeholderArtwork;
}

QString ItemWidgetFactoryHelpers::resolveSubcollectionTileArtwork(
    const QList<CollectionConfig> *collections, int subcollectionIndex,
    const QString &subcollectionName, const QString &parentArtworkDirectory) {
  // 1. The child's own collectionIcon, resolved through the shared seam all
  //    three consumers use (Kartend-dkh90).
  if (collections && subcollectionIndex >= 0 && subcollectionIndex < collections->size()) {
    const QString icon =
        CollectionUtils::resolvedCollectionIcon(collections->at(subcollectionIndex));
    if (!icon.isEmpty()) {
      return icon;
    }
  }
  // 2. An image named after the child, in the PARENT's artwork directory.
  if (subcollectionName.isEmpty() || parentArtworkDirectory.isEmpty()) {
    return {};
  }
  return ArtworkUtils::findArtworkForFile(subcollectionName, parentArtworkDirectory);
}

QString ItemWidgetFactory::resolvePlaceholderArtworkForCollection(int collectionIndex) const {
  // Memoized per collection index (negative results included): the
  // parent-chain walk + expandConfigVariables ran on every widget
  // materialization for a value that is constant until the collections list
  // or the context changes — both setters clear the cache. The computation
  // itself is the pure ItemWidgetFactoryHelpers policy function.
  const auto cached = m_placeholderArtworkCache.constFind(collectionIndex);
  if (cached != m_placeholderArtworkCache.constEnd()) {
    return cached.value();
  }
  const QString placeholderArtwork = ItemWidgetFactoryHelpers::resolvePlaceholderArtwork(
      m_collections, collectionIndex, m_context.config.placeholderArtwork, m_context.config.name);
  m_placeholderArtworkCache.insert(collectionIndex, placeholderArtwork);
  return placeholderArtwork;
}

void ItemWidgetFactory::applyPlaceholderArtwork(ItemWidget *widget,
                                                const QString &placeholderArtwork) const {
  if (!widget || placeholderArtwork.isEmpty() || widget->isListMode()) {
    return;
  }

  // QPixmap's file constructor routes through QPixmap::load, which serves
  // repeat loads of the same (path, mtime, size) from the global QPixmapCache
  // — the decode is paid once per placeholder image, not once per widget.
  QPixmap pixmap(placeholderArtwork);
  if (!pixmap.isNull()) {
    widget->setPlaceholderArtworkPixmap(pixmap);
  }
}

void ItemWidgetFactory::configureArtworkForWidget(ItemWidget *widget, const QString &fullPath,
                                                  bool forceDirectLookup) {
  QElapsedTimer perfTimer;
  if (lcPerfTrace().isDebugEnabled()) {
    perfTimer.start();
  }

  // A cover the user linked by hand outranks everything below (Kartend-1js9j).
  // It has to come first, not just before the name cascade: the session artwork
  // cache and the shift+middle-click type override are both name-based
  // auto-discovery, and a manual link beats auto-discovery in the sidebar
  // gallery, in loadItemDetail, and in items.artwork_path — the tile was the
  // one surface that disagreed. The map is prebuilt and existence-checked
  // (ItemArtworkStore::loadManualCoverPaths), so this stays a hash lookup on
  // the virtual-scroll hot path; the isEmpty() guard makes it free for the
  // libraries with no hand-linked artwork at all.
  //
  // LIST MODE takes the flag, not the pixmap: the row shows a preview BUTTON
  // rather than the cover. It used to be excluded here entirely, because the
  // overlay behind that button resolved the image by NAME and would have
  // opened nothing for a link-only item; ScrollManager::showArtworkPreview now
  // routes a linked item to the exact path instead, so the button opens the
  // cover the flag promises (Kartend-ni68u). Mirrors the list branch of
  // createMediaWidget, so a reconfigure pass agrees with first paint.
  if (!m_manualCoverPaths.isEmpty()) {
    const QString manualCover = m_manualCoverPaths.value(fullPath);
    if (!manualCover.isEmpty()) {
      if (widget && widget->isListMode()) {
        widget->setHasArtwork(true);
        return;
      }
      if (auto *art = artworkMgr()) {
        art->addPendingArtwork(widget, manualCover);
        return;
      }
    }
  }

  // Check cached artwork paths first (instant startup optimization)
  if (!m_cachedArtworkPaths.isEmpty()) {
    QString cachedPath = m_cachedArtworkPaths.value(fullPath);
    if (qEnvironmentVariableIsSet("KARTEND_ARTWORK_DIAG")) {
      qCDebug(lcSearchDiag) << "[ArtworkDiag] configureArtworkForWidget: fullPath=" << fullPath
                            << "cachedArtworkPaths.size=" << m_cachedArtworkPaths.size()
                            << "cachedPath=" << (cachedPath.isEmpty() ? "(not found)" : cachedPath);
    }
    // List mode displays an artwork preview button (not the pixmap), so
    // record that artwork exists rather than enqueueing a pixmap decode into
    // the hidden image label (mirrors the override and legacy exits below).
    if (widget && widget->isListMode() && !cachedPath.isEmpty()) {
      widget->setHasArtwork(true);
      return;
    }
    if (auto *art = artworkMgr(); art && !cachedPath.isEmpty()) {
      art->addPendingArtwork(widget, cachedPath);
      return;
    }
  }

  qint64 afterCacheCheck = lcPerfTrace().isDebugEnabled() ? perfTimer.elapsed() : 0;

  QString artworkDir = m_context.config.artworkDirectory;

  if (auto *db = dbMgr(); db && m_context.config.showAllSubcollectionItems) {
    QString foundArtworkDir = db->findArtworkDirectoryForFile(fullPath);
    if (!foundArtworkDir.isEmpty()) {
      artworkDir = foundArtworkDir;
    }
  }

  qint64 afterDirLookup = lcPerfTrace().isDebugEnabled() ? perfTimer.elapsed() : 0;

  // Mirror the media subfolder structure into the artwork directory when the
  // collection asks for it (includeArtworkSubfolders, or artwork co-located
  // with media). The shared helper also decides when NOT to mirror — an item
  // living outside the media directory has no media-relative subfolder, and
  // the `../../..` chain this used to build resolved outside the artwork tree
  // entirely (Kartend-j5amz). CoverFlowController resolves its cards through
  // the same helper so the two views never search different directories.
  artworkDir = ArtworkUtils::mirroredArtworkDirectory(
      artworkDir, m_context.config.mediaDirectory, fullPath,
      m_context.config.folderBrowsing.includeArtworkSubfolders);

  // if the user previously shift+middle-clicked this item to
  // cycle its artwork type, prefer the override over the legacy lookup so a
  // recycled widget reproduces the chosen type. The override map is the only
  // thing that survives widget pool churn — clearing happens in
  // ArtworkManager::clearWidgetReferences (collection change / pre-search).
  if (auto *art = artworkMgr()) {
    const QString overrideType = art->artworkTypeOverrideFor(fullPath);
    if (!overrideType.isEmpty()) {
      const QString baseName = QFileInfo(fullPath).completeBaseName();
      const QString overridePath =
          ItemArtworkStore::findStandardArtwork(baseName, artworkDir, overrideType);
      if (!overridePath.isEmpty()) {
        if (widget && widget->isListMode()) {
          widget->setHasArtwork(true);
          return;
        }
        art->addPendingArtwork(widget, overridePath);
        return;
      }
      // Override file vanished from disk — fall through to the legacy lookup
      // so the widget shows *something* instead of a blank tile.
    }
  }

  // Try cached directory lookup first for O(1) performance when cache is warm.
  // For large collections with subcollections, ONLY use cached lookup to avoid
  // blocking the UI with cold filesystem cache. The background prewarm will
  // populate the cache and reconfigure widgets afterward.
  // If forceDirectLookup is set, always do direct filesystem lookup (used after
  // prewarm).
  QString artworkPath;
  if (forceDirectLookup) {
    // Called from reconfigure after prewarm - OS cache should be warm
    artworkPath = ArtworkUtils::findArtworkForFile(QFileInfo(fullPath).fileName(), artworkDir);
  } else if (m_context.config.showAllSubcollectionItems && m_totalItemCount > 1000) {
    // Only use cached lookup - don't block UI with filesystem calls
    artworkPath =
        ArtworkUtils::findArtworkForFileCached(QFileInfo(fullPath).fileName(), artworkDir);
    // Don't fall back to direct lookup - let prewarm handle it
  } else if (ArtworkUtils::DirectoryCache::instance().isDirectoryCached(artworkDir)) {
    // Kartend-urrpp: the dir listing is already warm (early prewarm fires on
    // every collection selection now) — resolve via the O(1) cached lookup,
    // including cached NEGATIVES, instead of re-paying the multi-stat sweep
    // on every widget materialization while scrolling. First-ever misses
    // self-patch with a bounded probe inside findInDirectory.
    artworkPath =
        ArtworkUtils::findArtworkForFileCached(QFileInfo(fullPath).fileName(), artworkDir);
  } else if (!ArtworkUtils::DirectoryCache::instance().isDirectoryQueued(artworkDir)) {
    // Genuinely uncached dir with no prewarm in flight — do the direct
    // lookup so the tile paints artwork on the first frame (cold-start
    // before the early prewarm lands).
    artworkPath = ArtworkUtils::findArtworkForFile(QFileInfo(fullPath).fileName(), artworkDir);
  } else {
    // A background prewarm is queued for this dir (fast scroll over a cold
    // cache): skip the ~225-stat synchronous findArtworkForFile on the GUI
    // thread. Serve a cached hit if the prewarm already populated this entry;
    // misses are filled by the post-prewarm reconfigureArtworkForActiveWidgets()
    // (forceDirectLookup=true), mirroring the list-mode path (Kartend-bpkkm).
    artworkPath =
        ArtworkUtils::findArtworkForFileCached(QFileInfo(fullPath).fileName(), artworkDir);
  }

  qint64 afterArtworkFind = lcPerfTrace().isDebugEnabled() ? perfTimer.elapsed() : 0;

  if (lcPerfTrace().isDebugEnabled() && afterArtworkFind > 5) {
    qCDebug(lcPerfTrace) << "configureArtworkForWidget: totalMs=" << afterArtworkFind
                         << "cacheCheck=" << afterCacheCheck
                         << "dirLookup=" << (afterDirLookup - afterCacheCheck)
                         << "artworkFind=" << (afterArtworkFind - afterDirLookup)
                         << "artworkDir=" << artworkDir;
  }

  // List mode displays an artwork preview button (not the pixmap), so update
  // the per-widget hasArtwork flag rather than queueing a pixmap load. This is
  // also reached from reconfigureArtworkForActiveWidgets() after the directory
  // cache is warmed, which is the only chance list-mode widgets get to learn
  // their artwork exists when the cache was cold during initial creation
  //
  if (widget && widget->isListMode()) {
    widget->setHasArtwork(!artworkPath.isEmpty());
    return;
  }

  if (auto *art = artworkMgr(); art && !artworkPath.isEmpty()) {
    art->addPendingArtwork(widget, artworkPath);
  } else if (lcPerfTrace().isDebugEnabled() && artworkPath.isEmpty()) {
    static int emptyCount = 0;
    if (++emptyCount <= 5) { // Only log first 5 to avoid spam
      qCDebug(lcPerfTrace) << "configureArtworkForWidget: NO ARTWORK artworkDir=" << artworkDir
                           << "fileName=" << QFileInfo(fullPath).fileName();
    }
  }
}
