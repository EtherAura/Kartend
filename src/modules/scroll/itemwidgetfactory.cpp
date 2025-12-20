// Factory for creating and configuring ItemWidget instances.
#include "itemwidgetfactory.h"

#include "artworkmanager.h"
#include "artworkutils.h"
#include "databasemanager.h"
#include "itemwidget.h"
#include "uiconstants.h"
#include "widgetpoolmanager.h"

#include <QDir>
#include <QFileInfo>
#include <QtGlobal>

ItemWidgetFactory::ItemWidgetFactory(QObject *parent) : QObject(parent) {}

void ItemWidgetFactory::setMetrics(int itemWidth, int itemHeight) {
  m_itemWidth = itemWidth;
  m_itemHeight = itemHeight;
}

void ItemWidgetFactory::setFileData(const QStringList *filePaths,
                                    const QHash<QString, QString> *fileNames) {
  m_filePaths = filePaths;
  m_fileNames = fileNames;
}

void ItemWidgetFactory::setCachedArtworkPaths(const QHash<QString, QString> &artworkPaths) {
  m_cachedArtworkPaths = artworkPaths;
}

ItemWidget *ItemWidgetFactory::acquireWidget() {
  // Cannot create widgets without a valid parent
  if (!m_parentWidget) {
    qWarning() << "ItemWidgetFactory::acquireWidget: m_parentWidget is null, cannot acquire widget";
    return nullptr;
  }
  if (!m_widgetPool) {
    return new ItemWidget(m_parentWidget);
  }
  m_widgetPool->setWidgetParent(m_parentWidget);
  return m_widgetPool->acquire();
}

void ItemWidgetFactory::configureBaseWidget(ItemWidget *widget) {
  // Set parent BEFORE resetForReuse() to ensure child widgets (imageLabel, etc.)
  // are in a valid state - reparenting first prevents stale child pointers
  if (m_parentWidget) {
    widget->setParent(m_parentWidget);
  }
  widget->resetForReuse();
  widget->setFocusPolicy(Qt::NoFocus);
  widget->setHideTitles(m_context.config.hideTitles);
  widget->setHideSubcollectionTitles(m_context.config.hideSubcollectionTitles);
  widget->setFontSize(m_context.config.fontSize);
  widget->setCornerRadius(m_context.config.cornerRadius);
  widget->setItemDimensions(m_itemWidth, m_itemHeight);
  // Force artwork refresh after all configuration is set to ensure
  // corner radius and other settings are applied to the placeholder
  widget->onArtworkChanged();
}

void ItemWidgetFactory::releaseWidget(ItemWidget *widget, int visibleRows,
                                      int itemsPerRow) {
  if (!widget) {
    return;
  }
  if (m_widgetPool) {
    m_widgetPool->setVisibleMetrics(visibleRows, itemsPerRow);
    m_widgetPool->release(widget);
  } else {
    widget->deleteLater();
  }
}

ItemWidget *ItemWidgetFactory::createSubcollectionWidget(int subcollectionIndex) {
  auto *widget = acquireWidget();
  if (!widget) {
    return nullptr;
  }
  configureBaseWidget(widget);

  QString subcollectionName;
  if (m_subcollectionNameResolver) {
    subcollectionName = m_subcollectionNameResolver(subcollectionIndex);
  }
  widget->setAsSubcollection(subcollectionIndex, subcollectionName);

  // Try to find artwork for the subcollection using the folder name
  // Artwork is searched in the current collection's artwork directory
  if (!subcollectionName.isEmpty() && m_artworkManager) {
    QString artworkDir = m_context.config.artworkDirectory;
    if (!artworkDir.isEmpty()) {
      QString artworkPath = ArtworkUtils::findArtworkForFile(
          subcollectionName, artworkDir);
      if (!artworkPath.isEmpty()) {
        m_artworkManager->addPendingArtwork(widget, artworkPath);
      }
    }
  }

  // Connect double-click signal
  connect(widget, &ItemWidget::subcollectionDoubleClicked, this,
          &ItemWidgetFactory::subcollectionDoubleClicked);

  return widget;
}

ItemWidget *ItemWidgetFactory::createVirtualFolderWidget(const QString &folderPath) {
  auto *widget = acquireWidget();
  if (!widget) {
    return nullptr;
  }
  configureBaseWidget(widget);

  // Extract display name from folder path (last component)
  QString displayName = folderPath;
  int lastSlash = folderPath.lastIndexOf('/');
  if (lastSlash >= 0) {
    displayName = folderPath.mid(lastSlash + 1);
  }

  widget->setAsVirtualFolder(folderPath, displayName, m_context.config.hideSubfolderTitles);

  // Try to find artwork for the virtual folder using the folder name
  // Artwork is searched in the current collection's artwork directory
  if (!displayName.isEmpty() && m_artworkManager) {
    QString artworkDir = m_context.config.artworkDirectory;
    if (!artworkDir.isEmpty()) {
      QString artworkPath = ArtworkUtils::findArtworkForFile(
          displayName, artworkDir);
      if (!artworkPath.isEmpty()) {
        m_artworkManager->addPendingArtwork(widget, artworkPath);
      }
    }
  }

  // Connect double-click signal
  connect(widget, &ItemWidget::virtualFolderDoubleClicked, this,
          &ItemWidgetFactory::virtualFolderDoubleClicked);

  return widget;
}

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
      if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
        qWarning() << "[SearchDiag][ItemWidgetFactory] requestItemsRange: mediaIndex="
                   << mediaIndex << "chunkStart=" << chunkStart
                   << "chunkSize=" << chunkSize;
      }
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

  configureArtworkForWidget(widget, fullPath);

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
      displayName =
          m_fileNames
              ? m_fileNames->value(fullPath, QFileInfo(fullPath)
                                                 .completeBaseName()
                                                 .replace('_', ' ')
                                                 .simplified())
              : QFileInfo(fullPath)
                    .completeBaseName()
                    .replace('_', ' ')
                    .simplified();
    } else {
      displayName = m_fileNames
                        ? m_fileNames->value(fullPath,
                                             QFileInfo(rawFileName).completeBaseName())
                        : QFileInfo(rawFileName).completeBaseName();
    }

    // When searching with virtual folders, prepend the subfolder path to help
    // identify which folder the result came from. This applies when:
    // - Collection uses virtual folders (includeContentSubfolders && !showAllSubfolderItems)
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
      qWarning() << "[ArtworkDiag] configureArtworkForWidget: fullPath=" << fullPath
                 << "cachedArtworkPaths.size=" << m_cachedArtworkPaths.size()
                 << "cachedPath=" << (cachedPath.isEmpty() ? "(not found)" : cachedPath);
    }
    if (!cachedPath.isEmpty() && m_artworkManager) {
      m_artworkManager->addPendingArtwork(widget, cachedPath);
      return;
    }
  }
  
  qint64 afterCacheCheck = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") ? perfTimer.elapsed() : 0;
  
  QString artworkDir = m_context.config.artworkDirectory;

  if (m_databaseManager && m_context.config.showAllSubcollectionItems) {
    QString foundArtworkDir =
        m_databaseManager->findArtworkDirectoryForFile(fullPath);
    if (!foundArtworkDir.isEmpty()) {
      artworkDir = foundArtworkDir;
    }
  }
  
  qint64 afterDirLookup = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") ? perfTimer.elapsed() : 0;

  // Mirror the subfolder structure from media directory to artwork directory when:
  // 1. includeArtworkSubfolders is explicitly enabled, OR
  // 2. artworkDirectory equals mediaDirectory (artwork is co-located with media)
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
  // If forceDirectLookup is set, always do direct filesystem lookup (used after prewarm).
  QString artworkPath;
  if (forceDirectLookup) {
    // Called from reconfigure after prewarm - OS cache should be warm
    artworkPath = ArtworkUtils::findArtworkForFile(
        QFileInfo(fullPath).fileName(), artworkDir);
  } else if (m_context.config.showAllSubcollectionItems && m_totalItemCount > 1000) {
    // Only use cached lookup - don't block UI with filesystem calls
    artworkPath = ArtworkUtils::findArtworkForFileCached(
        QFileInfo(fullPath).fileName(), artworkDir);
    // Don't fall back to direct lookup - let prewarm handle it
  } else {
    artworkPath = ArtworkUtils::findArtworkForFile(
        QFileInfo(fullPath).fileName(), artworkDir);
  }
  
  qint64 afterArtworkFind = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") ? perfTimer.elapsed() : 0;
  
  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") && afterArtworkFind > 5) {
    qWarning() << "[PerfTrace] configureArtworkForWidget: totalMs=" << afterArtworkFind
               << "cacheCheck=" << afterCacheCheck
               << "dirLookup=" << (afterDirLookup - afterCacheCheck)
               << "artworkFind=" << (afterArtworkFind - afterDirLookup)
               << "artworkDir=" << artworkDir;
  }

  if (!artworkPath.isEmpty() && m_artworkManager) {
    m_artworkManager->addPendingArtwork(widget, artworkPath);
  } else if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") && artworkPath.isEmpty()) {
    static int emptyCount = 0;
    if (++emptyCount <= 5) {  // Only log first 5 to avoid spam
      qWarning() << "[PerfTrace] configureArtworkForWidget: NO ARTWORK artworkDir=" << artworkDir
                 << "fileName=" << QFileInfo(fullPath).fileName();
    }
  }
}

int ItemWidgetFactory::computeChunkSize() const {
  // Use larger chunks for showAllSubcollectionItems with many items
  // to reduce database round-trips when flattening large collections
  if (m_context.config.showAllSubcollectionItems &&
      m_totalItemCount > UIConstants::Database::RANGE_CHUNK_LARGE_THRESHOLD) {
    return UIConstants::Database::RANGE_CHUNK_SIZE_LARGE;
  }
  return UIConstants::Database::RANGE_CHUNK_SIZE_DEFAULT;
}

void ItemWidgetFactory::prefetchAdjacentChunks(int currentMediaIndex, int chunkSize) {
  if (!m_filePaths) {
    return;
  }
  
  const int fileCount = m_filePaths->size();
  const int prefetchCount = UIConstants::Database::RANGE_PREFETCH_CHUNKS;
  
  // Calculate current chunk
  int currentChunk = currentMediaIndex / chunkSize;
  
  // Prefetch chunks ahead and behind current position
  for (int i = 1; i <= prefetchCount; ++i) {
    // Prefetch ahead
    int aheadChunk = currentChunk + i;
    int aheadStart = aheadChunk * chunkSize;
    if (aheadStart < fileCount && !m_pendingRangeRequests.contains(aheadStart)) {
      // Check if this chunk has unloaded items
      if (aheadStart < m_filePaths->size() && m_filePaths->at(aheadStart).isEmpty()) {
        m_pendingRangeRequests.insert(aheadStart);
        emit requestItemsRange(aheadStart, chunkSize);
      }
    }
    
    // Prefetch behind (useful for scroll-up after scroll-down)
    int behindChunk = currentChunk - i;
    if (behindChunk >= 0) {
      int behindStart = behindChunk * chunkSize;
      if (!m_pendingRangeRequests.contains(behindStart)) {
        // Check if this chunk has unloaded items
        if (behindStart < m_filePaths->size() && m_filePaths->at(behindStart).isEmpty()) {
          m_pendingRangeRequests.insert(behindStart);
          emit requestItemsRange(behindStart, chunkSize);
        }
      }
    }
  }
}

void ItemWidgetFactory::prefetchRangeAt(int startIndex, int count) {
  // Prefetch data for a specific range, typically called during scrollbar drag.
  // This allows starting database queries before the user releases the scrollbar.
  if (!m_filePaths || startIndex < 0) {
    return;
  }
  
  const int fileCount = m_filePaths->size();
  if (startIndex >= fileCount) {
    return;
  }
  
  // Only request if not already pending and the chunk has unloaded items
  if (!m_pendingRangeRequests.contains(startIndex)) {
    if (m_filePaths->at(startIndex).isEmpty()) {
      m_pendingRangeRequests.insert(startIndex);
      emit requestItemsRange(startIndex, count);
      
      // Also prefetch adjacent chunks to improve continuity
      prefetchAdjacentChunks(startIndex, count);
    }
  }
}
