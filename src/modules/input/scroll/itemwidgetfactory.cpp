// Factory for creating and configuring ItemWidget instances.
#include "itemwidgetfactory.h"

#include "artworkutils.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "itemwidget.h"
#include "textzoom.h"
#include "uiconstants.h"
#include "widgetpoolmanager.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QtGlobal>

Q_DECLARE_LOGGING_CATEGORY(lcScrollManager)

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
    qCWarning(lcScrollManager) << "ItemWidgetFactory::acquireWidget: m_parentWidget is null, "
                                  "cannot acquire widget";
    return nullptr;
  }
  if (!m_widgetPool) {
    return new ItemWidget(m_parentWidget);
  }
  m_widgetPool->setWidgetParent(m_parentWidget);
  return m_widgetPool->acquire();
}

void ItemWidgetFactory::configureBaseWidget(ItemWidget *widget) {
  // Set parent BEFORE resetForReuse() to ensure child widgets (imageLabel,
  // etc.) are in a valid state - reparenting first prevents stale child
  // pointers
  if (m_parentWidget) {
    widget->setParent(m_parentWidget);
  }
  widget->resetForReuse();
  widget->setFocusPolicy(Qt::NoFocus);
  widget->setHideTitles(m_context.config.hideTitles);
  widget->setHideSubcollectionTitles(m_context.config.hideSubcollectionTitles);

  // Set list mode based on collection viewType - must be set before
  // dimensions/font
  bool isListMode = (m_context.config.viewType == ViewType::List);
  widget->setListMode(isListMode);

  // Set collection column width for list mode (synced from header drag-resize)
  if (isListMode) {
    widget->setCollectionColumnWidth(m_collectionColumnWidth);
    widget->setArtworkColumnWidth(m_artworkColumnWidth);
  }

  // Use list-specific font size in list mode, grid font size otherwise.
  // layer the runtime text-zoom multiplier on top so item
  // titles scale alongside menus/dialogs/sidebar.
  int fontSize =
      isListMode ? m_context.config.listView.listFontSize : m_context.config.gridLayout.fontSize;
  widget->setFontSize(TextZoom::zoomedFontSize(fontSize));

  widget->setCornerRadius(m_context.config.gridLayout.cornerRadius);
  widget->setItemDimensions(m_itemWidth, m_itemHeight);
  // Force artwork refresh after all configuration is set to ensure
  // corner radius and other settings are applied to the placeholder
  // Skip for list mode since artwork is hidden
  if (!isListMode) {
    widget->onArtworkChanged();
  }
}

void ItemWidgetFactory::releaseWidget(ItemWidget *widget, int visibleRows, int itemsPerRow) {
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

  // Set current collection name for list mode display
  if (m_context.config.viewType == ViewType::List && m_collections && m_context.currentIndex >= 0 &&
      m_context.currentIndex < m_collections->size()) {
    widget->setCollectionName(m_collections->at(m_context.currentIndex).name);
  }

  // Try to find artwork for the subcollection using the folder name
  // Artwork is searched in the current collection's artwork directory
  if (!subcollectionName.isEmpty() && m_artworkManager) {
    QString artworkDir = m_context.config.artworkDirectory;
    const QString placeholderArtwork =
        resolvePlaceholderArtworkForCollection(subcollectionIndex).trimmed();
    if (!artworkDir.isEmpty()) {
      QString artworkPath = ArtworkUtils::findArtworkForFile(subcollectionName, artworkDir);
      if (!artworkPath.isEmpty()) {
        m_artworkManager->addPendingArtwork(widget, artworkPath);
        // Set hasArtwork for list mode button
        if (m_context.config.viewType == ViewType::List) {
          widget->setHasArtwork(true);
        }
      }
    }
    applyPlaceholderArtwork(widget, placeholderArtwork);
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

  widget->setAsVirtualFolder(folderPath, displayName,
                             m_context.config.folderBrowsing.hideSubfolderTitles);

  // Try to find artwork for the virtual folder using the folder name
  // Artwork is searched in the current collection's artwork directory
  if (!displayName.isEmpty() && m_artworkManager) {
    QString artworkDir = m_context.config.artworkDirectory;
    const QString placeholderArtwork =
        resolvePlaceholderArtworkForCollection(m_context.currentIndex).trimmed();
    if (!artworkDir.isEmpty()) {
      QString artworkPath = ArtworkUtils::findArtworkForFile(displayName, artworkDir);
      if (!artworkPath.isEmpty()) {
        m_artworkManager->addPendingArtwork(widget, artworkPath);
      }
    }
    applyPlaceholderArtwork(widget, placeholderArtwork);
  }

  // Connect double-click signal
  connect(widget, &ItemWidget::virtualFolderDoubleClicked, this,
          &ItemWidgetFactory::virtualFolderDoubleClicked);

  return widget;
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
  // This allows starting database queries before the user releases the
  // scrollbar.
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
