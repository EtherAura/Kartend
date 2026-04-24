// Widget creation + media path resolution + artwork configuration cluster
// split out from itemwidgetfactory.cpp.
#include "itemwidgetfactory.h"

#include "loggingcategories.h"
#include "artworkmanager.h"
#include "artworkutils.h"
#include "databasemanager.h"
#include "itemwidget.h"
#include "uiconstants.h"
#include "widgetpoolmanager.h"

#include <QDir>
#include <QFileInfo>
#include <QtGlobal>

ItemWidget *ItemWidgetFactory::createMediaWidget(int mediaIndex,
                                                 int &collectionIndex) {
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
        qCDebug(lcSearchDiag) << "[ItemWidgetFactory] requestItemsRange: mediaIndex="
            << mediaIndex << "chunkStart=" << chunkStart
            << "chunkSize=" << chunkSize;
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
  if (m_context.config.viewType == ViewType::List && m_collections &&
      collectionIndex >= 0 && collectionIndex < m_collections->size()) {
    widget->setCollectionName(m_collections->at(collectionIndex).name);
  }

  // Skip artwork loading in list mode - but check if artwork exists for the
  // icon
  if (m_context.config.viewType != ViewType::List) {
    configureArtworkForWidget(widget, fullPath);
  } else {
    // In list mode, check if artwork exists to show the preview button
    // Need to look up artwork from the item's collection, not the current
    // collection
    QString artworkDir;
    if (m_collections && collectionIndex >= 0 &&
        collectionIndex < m_collections->size()) {
      artworkDir = m_collections->at(collectionIndex).artworkDirectory;
    }
    if (artworkDir.isEmpty()) {
      artworkDir = m_context.config.artworkDirectory;
    }
    // Store artwork directory for preview overlay to use
    widget->setArtworkDirectory(artworkDir);
    if (!artworkDir.isEmpty()) {
      QString fileName = QFileInfo(fullPath).fileName();
      QString artworkPath =
          ArtworkUtils::findArtworkForFileCached(fileName, artworkDir);
      // Cold-cache fallback: findArtworkForFileCached returns empty on the
      // first lookup for an uncached directory (it queues a background scan).
      // For list mode we can't rely on the post-prewarm reconfigure alone --
      // do a direct synchronous lookup so the artwork preview button
      // ('view-preview' icon) appears on first paint (Kartend-cbd).
      if (artworkPath.isEmpty()) {
        artworkPath = ArtworkUtils::findArtworkForFile(fileName, artworkDir);
      }
      widget->setHasArtwork(!artworkPath.isEmpty());
    }
  }

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

void ItemWidgetFactory::resolveMediaItemPaths(const QString &rawFileName,
                                              QString &fullPath,
                                              QString &displayName,
                                              int &collectionIndex) {
  if (!m_databaseManager) {
    return;
  }

  // Use DatabaseManager for path resolution
  fullPath = m_databaseManager->resolveFilePath(rawFileName, m_context);

  if (!fullPath.isEmpty()) {
    if (m_context.config.showAllSubcollectionItems) {
      displayName = m_fileNames
                        ? m_fileNames->value(fullPath, QFileInfo(fullPath)
                                                           .completeBaseName()
                                                           .replace('_', ' ')
                                                           .simplified())
                        : QFileInfo(fullPath)
                              .completeBaseName()
                              .replace('_', ' ')
                              .simplified();
    } else {
      displayName =
          m_fileNames ? m_fileNames->value(
                            fullPath, QFileInfo(rawFileName).completeBaseName())
                      : QFileInfo(rawFileName).completeBaseName();
    }

    // When searching with virtual folders, prepend the subfolder path to help
    // identify which folder the result came from. This applies when:
    // - Collection uses virtual folders (includeContentSubfolders &&
    // !showAllSubfolderItems)
    // - Search is active (suppressVirtualFolders is set during search)
    if (m_context.suppressVirtualFolders &&
        m_context.config.includeContentSubfolders &&
        !m_context.config.showAllSubfolderItems) {
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

void ItemWidgetFactory::updateCollectionIndexFromDatabase(
    const QString &fullPath, int &collectionIndex) {
  if (m_databaseManager) {
    int detectedCollectionIndex =
        m_databaseManager->getCollectionIndexForFile(fullPath);
    if (detectedCollectionIndex >= 0) {
      collectionIndex = detectedCollectionIndex;
    }
  }
}

void ItemWidgetFactory::configureArtworkForWidget(ItemWidget *widget,
                                                  const QString &fullPath,
                                                  bool forceDirectLookup) {
  QElapsedTimer perfTimer;
  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    perfTimer.start();
  }

  // Check cached artwork paths first (instant startup optimization)
  if (!m_cachedArtworkPaths.isEmpty()) {
    QString cachedPath = m_cachedArtworkPaths.value(fullPath);
    if (qEnvironmentVariableIsSet("KARTEND_ARTWORK_DIAG")) {
      qCDebug(lcSearchDiag) << "[ArtworkDiag] configureArtworkForWidget: fullPath="
                 << fullPath
                 << "cachedArtworkPaths.size=" << m_cachedArtworkPaths.size()
                 << "cachedPath="
                 << (cachedPath.isEmpty() ? "(not found)" : cachedPath);
    }
    if (!cachedPath.isEmpty() && m_artworkManager) {
      m_artworkManager->addPendingArtwork(widget, cachedPath);
      return;
    }
  }

  qint64 afterCacheCheck =
      qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") ? perfTimer.elapsed() : 0;

  QString artworkDir = m_context.config.artworkDirectory;

  if (m_databaseManager && m_context.config.showAllSubcollectionItems) {
    QString foundArtworkDir =
        m_databaseManager->findArtworkDirectoryForFile(fullPath);
    if (!foundArtworkDir.isEmpty()) {
      artworkDir = foundArtworkDir;
    }
  }

  qint64 afterDirLookup =
      qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") ? perfTimer.elapsed() : 0;

  // Mirror the subfolder structure from media directory to artwork directory
  // when:
  // 1. includeArtworkSubfolders is explicitly enabled, OR
  // 2. artworkDirectory equals mediaDirectory (artwork is co-located with
  // media)
  const QString &mediaDir = m_context.config.mediaDirectory;
  bool shouldMirrorSubfolders =
      m_context.config.includeArtworkSubfolders ||
      (QDir(artworkDir).absolutePath() == QDir(mediaDir).absolutePath());

  if (shouldMirrorSubfolders && !mediaDir.isEmpty()) {
    QDir mediaDirObj(mediaDir);
    QString relativePath = mediaDirObj.relativeFilePath(fullPath);
    // Extract the directory component (subfolder path without the filename)
    QString relativeDir = QFileInfo(relativePath).path();
    if (!relativeDir.isEmpty() && relativeDir != ".") {
      artworkDir = QDir(artworkDir).absoluteFilePath(relativeDir);
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
    artworkPath = ArtworkUtils::findArtworkForFile(
        QFileInfo(fullPath).fileName(), artworkDir);
  } else if (m_context.config.showAllSubcollectionItems &&
             m_totalItemCount > 1000) {
    // Only use cached lookup - don't block UI with filesystem calls
    artworkPath = ArtworkUtils::findArtworkForFileCached(
        QFileInfo(fullPath).fileName(), artworkDir);
    // Don't fall back to direct lookup - let prewarm handle it
  } else {
    artworkPath = ArtworkUtils::findArtworkForFile(
        QFileInfo(fullPath).fileName(), artworkDir);
  }

  qint64 afterArtworkFind =
      qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") ? perfTimer.elapsed() : 0;

  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") && afterArtworkFind > 5) {
    qCDebug(lcPerfTrace) << "configureArtworkForWidget: totalMs="
               << afterArtworkFind << "cacheCheck=" << afterCacheCheck
               << "dirLookup=" << (afterDirLookup - afterCacheCheck)
               << "artworkFind=" << (afterArtworkFind - afterDirLookup)
               << "artworkDir=" << artworkDir;
  }

  // List mode displays an artwork preview button (not the pixmap), so update
  // the per-widget hasArtwork flag rather than queueing a pixmap load. This is
  // also reached from reconfigureArtworkForActiveWidgets() after the directory
  // cache is warmed, which is the only chance list-mode widgets get to learn
  // their artwork exists when the cache was cold during initial creation
  // (Kartend-cbd).
  if (widget && widget->isListMode()) {
    widget->setHasArtwork(!artworkPath.isEmpty());
    return;
  }

  if (!artworkPath.isEmpty() && m_artworkManager) {
    m_artworkManager->addPendingArtwork(widget, artworkPath);
  } else if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") &&
             artworkPath.isEmpty()) {
    static int emptyCount = 0;
    if (++emptyCount <= 5) { // Only log first 5 to avoid spam
      qCDebug(lcPerfTrace) << "configureArtworkForWidget: NO ARTWORK artworkDir="
          << artworkDir << "fileName=" << QFileInfo(fullPath).fileName();
    }
  }
}

