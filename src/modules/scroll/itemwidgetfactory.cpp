// Factory for creating and configuring ItemWidget instances.
#include "itemwidgetfactory.h"

#include "artworkmanager.h"
#include "artworkutils.h"
#include "databasemanager.h"
#include "itemwidget.h"
#include "widgetpoolmanager.h"

#include <QDir>
#include <QFileInfo>

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
    constexpr int chunkSize = 100;
    int chunkStart = (mediaIndex / chunkSize) * chunkSize;
    emit requestItemsRange(chunkStart, chunkSize);
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
                                                  const QString &fullPath) {
  QString artworkDir = m_context.config.artworkDirectory;
  if (m_databaseManager && m_context.config.showAllSubcollectionItems) {
    QString foundArtworkDir =
        m_databaseManager->findArtworkDirectoryForFile(fullPath);
    if (!foundArtworkDir.isEmpty()) {
      artworkDir = foundArtworkDir;
    }
  }

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

  QString artworkPath = ArtworkUtils::findArtworkForFile(
      QFileInfo(fullPath).fileName(), artworkDir);
  if (!artworkPath.isEmpty() && m_artworkManager) {
    m_artworkManager->addPendingArtwork(widget, artworkPath);
  }
}
