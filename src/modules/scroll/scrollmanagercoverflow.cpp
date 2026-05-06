// Kartend-3ile: ScrollManager glue for the CoverFlow ViewType.
//
// CoverFlowWidget lives as a sibling of gridContainer in the items page
// scroll-area layout. When the active collection's view type is CoverFlow we
// hide gridContainer + the scrollbars and show the cover-flow widget; when
// the user switches back to Grid/List we reverse it. The widget owns its
// own selection animation and artwork loading, so the only state ScrollManager
// pushes in is the card list and the canonical selected index — selection
// changes from user input round-trip back through SelectionManager via the
// existing selectItemByIndex signal so sidebar/restore/persistence stay
// coherent.

#include "scrollmanager.h"

#include "artworkutils.h"
#include "collectionutils.h"
#include "coverflowwidget.h"
#include "databasemanager.h"
#include "filtermanager.h"
#include "itemartwork.h"
#include "mainwindow.h"
#include "pathutils.h"
#include "scrolldatamanager.h"
#include "videoutils.h"

#include <QBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QScrollArea>
#include <QScrollBar>

bool ScrollManager::coverFlowActive() const {
  return m_context.config.viewType == ViewType::CoverFlow;
}

void ScrollManager::ensureCoverFlowWidget() {
  if (m_coverFlowWidget) {
    return;
  }
  if (!m_mediaScrollArea) {
    return;
  }
  // Parent to the scroll area's parent (m_mainContentWidget) so the carousel
  // sits as a sibling of itemScrollArea rather than nested inside it. This
  // avoids the scroll-area widgetResizable / viewport pipeline that was
  // suppressing paintEvent dispatch when the widget lived inside
  // itemScrollAreaWidgetContents.
  QWidget *contentParent = m_mediaScrollArea->parentWidget();
  if (!contentParent) {
    return;
  }
  m_coverFlowWidget = new CoverFlowWidget(contentParent);
  m_coverFlowWidget->hide();

  // Insert into the content widget's layout right before the metadata
  // sidebar, so the carousel takes the same horizontal slot the scroll
  // area otherwise occupies. The scroll area is still in the layout but
  // hidden when cover flow is active.
  if (auto *layout = qobject_cast<QBoxLayout *>(contentParent->layout())) {
    int idx = layout->indexOf(m_mediaScrollArea);
    if (idx >= 0) {
      layout->insertWidget(idx + 1, m_coverFlowWidget);
    } else {
      layout->addWidget(m_coverFlowWidget);
    }
  }

  // Selection requested by user input → run through the existing selection
  // pipeline so sidebar/restore/persistence stay coherent. The canonical
  // index then comes back to us via updateSelectionForIndex().
  connect(m_coverFlowWidget, &CoverFlowWidget::selectionChangeRequested, this,
          [this](int idx) { emit selectItemByIndex(idx); });

  // Activation: subcollections and virtual folders use their own navigation
  // paths; everything else is a media item launch routed through MainWindow.
  connect(m_coverFlowWidget, &CoverFlowWidget::itemActivated, this, [this](int idx) {
    if (!m_dataManager) {
      return;
    }
    if (m_dataManager->isSubcollectionIndex(idx)) {
      int sub = m_dataManager->subcollectionIndexFromActual(idx);
      if (sub >= 0) {
        emit subcollectionEntered(sub);
      }
      return;
    }
    if (m_dataManager->isVirtualFolderIndex(idx)) {
      QString folder = m_dataManager->virtualFolderFromActual(idx);
      if (!folder.isEmpty()) {
        emit virtualFolderEntered(folder);
      }
      return;
    }
    emit coverFlowItemActivated(idx);
  });
}

void ScrollManager::applyCoverFlowConfig() {
  if (!m_coverFlowWidget) {
    return;
  }
  const auto &cfg = m_context.config;
  m_coverFlowWidget->setCornerRadius(cfg.cornerRadius);
  m_coverFlowWidget->setTileColor(cfg.tileColor);
  m_coverFlowWidget->setSelectionColor(cfg.selectionColor);
  m_coverFlowWidget->setBackgroundColor(cfg.backgroundColor);
  m_coverFlowWidget->setHideTitles(cfg.hideTitles);
  // Kartend-7eff: scale the cover-flow caption alongside grid/list items.
  m_coverFlowWidget->setFontSize(MainWindow::zoomedFontSize(cfg.fontSize));
  m_coverFlowWidget->setFontFamily(cfg.customFontFamily);
}

void ScrollManager::applyCoverFlowVisibility() {
  const bool active = coverFlowActive();
  if (active) {
    ensureCoverFlowWidget();
  }
  if (m_coverFlowWidget) {
    m_coverFlowWidget->setVisible(active);
    if (active) {
      m_coverFlowWidget->raise();
      m_coverFlowWidget->update();
    }
  }
  // When cover flow is active, hide the entire scroll area (carousel takes
  // its slot in m_mainContentWidget). When inactive, show the scroll area
  // and let the scroll-area-internal grid/list visibility be governed by
  // gridContainer alone.
  if (m_mediaScrollArea) {
    m_mediaScrollArea->setVisible(!active);
  }
  if (m_gridContainer) {
    m_gridContainer->setVisible(!active);
  }
  if (m_mediaScrollArea) {
    auto policy = active ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded;
    m_mediaScrollArea->setVerticalScrollBarPolicy(policy);
    // Horizontal scrollbar is independently controlled by per-collection
    // settings; only force it off in cover flow to prevent stray bars.
    if (active) {
      m_mediaScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    } else {
      m_mediaScrollArea->setHorizontalScrollBarPolicy(m_context.config.hideHorizontalScrollbar
                                                          ? Qt::ScrollBarAlwaysOff
                                                          : Qt::ScrollBarAsNeeded);
    }
  }
  if (active && m_coverFlowWidget) {
    m_coverFlowWidget->setFocus();
    // Pre-resolve the video and gallery for the currently centered card
    // so the very first middle-click can play it without a one-cycle
    // delay waiting for selectionChanged → resolve* to fire.
    const int initialIdx = m_coverFlowWidget->selectedIndex();
    resolveAndPushCoverFlowVideo(initialIdx);
    resolveAndPushCoverFlowGallery(initialIdx);
  }
  // Notify listeners (MainWindow → SidebarManager) so the sidebar can yield
  // viewport space to the carousel without overwriting the persisted
  // per-collection sidebarVisible preference.
  emit coverFlowActiveChanged(active);
}

// Resolve the artwork path for a single media item. Mirrors the lookup
// logic in ItemWidgetFactory::configureArtworkForWidget (per-item override
// for showAllSubcollectionItems, subfolder mirroring when artworkDir ==
// mediaDir or includeArtworkSubfolders is set) — but uses the directory
// cache only. The synchronous filesystem fallback that the grid factory
// employs for visible widgets is too expensive here: cover flow rebuilds
// the entire card list on every navigation/filter/range-load event, and
// running ArtworkUtils::findArtworkForFile across tens of thousands of
// items on the UI thread freezes the app for seconds. The directory
// cache is warmed by ArtworkManager's prewarm pipeline; entries that
// resolve to empty here will be picked up on the next rebuild after the
// cache populates (rebuildCoverFlowCards runs on each receiveItemsRange).
static QString resolveCardArtworkPath(const QString &fullPath, const CollectionContext &context,
                                      DatabaseManager *db) {
  if (fullPath.isEmpty()) {
    return {};
  }
  QString artworkDir = context.config.artworkDirectory;
  if (db && context.config.showAllSubcollectionItems) {
    QString found = db->findArtworkDirectoryForFile(fullPath);
    if (!found.isEmpty()) {
      artworkDir = found;
    }
  }
  if (artworkDir.isEmpty()) {
    return {};
  }
  const QString &mediaDir = context.config.mediaDirectory;
  const bool shouldMirror =
      context.config.includeArtworkSubfolders ||
      (!mediaDir.isEmpty() && QDir(artworkDir).absolutePath() == QDir(mediaDir).absolutePath());
  if (shouldMirror && !mediaDir.isEmpty()) {
    QDir mediaDirObj(mediaDir);
    QString relativePath = mediaDirObj.relativeFilePath(fullPath);
    QString relativeDir = QFileInfo(relativePath).path();
    if (!relativeDir.isEmpty() && relativeDir != QStringLiteral(".")) {
      artworkDir = QDir(artworkDir).absoluteFilePath(relativeDir);
    }
  }
  const QString fileName = QFileInfo(fullPath).fileName();
  return ArtworkUtils::findArtworkForFileCached(fileName, artworkDir);
}

void ScrollManager::resolveAndPushCoverFlowVideo(int visualIndex) {
  if (!m_coverFlowWidget || !m_dataManager) {
    return;
  }
  // Only media items have preview videos — subcollection / virtual-folder
  // cards never carry one.
  const bool filtered = m_filterManager && m_filterManager->isFiltered();
  const int actualIndex = filtered ? m_filterManager->getActualIndex(visualIndex) : visualIndex;
  if (actualIndex < 0 || !m_dataManager->isMediaIndex(actualIndex)) {
    m_coverFlowWidget->setVideoPathForIndex(visualIndex, QString());
    return;
  }
  const int mediaIdx = m_dataManager->mediaIndexFromActual(actualIndex);
  const QString rawEntry = m_dataManager->rawFilePath(mediaIdx);
  const QString fullPath =
      m_databaseManager ? m_databaseManager->resolveFilePath(rawEntry, m_context) : rawEntry;
  if (fullPath.isEmpty()) {
    m_coverFlowWidget->setVideoPathForIndex(visualIndex, QString());
    return;
  }
  // Mirror SidebarManager's owner-aware resolution so showAllSubcollectionItems
  // collections find videos stored under the child collection's videoDirectory
  // even though the active context is the parent.
  QString videoDirectory = m_context.config.videoDirectory;
  QString videoExpansionName = m_context.config.name;
  if (m_databaseManager) {
    const int owningIndex = m_databaseManager->getCollectionIndexForFile(fullPath);
    if (owningIndex >= 0 && m_collections && owningIndex < m_collections->size()) {
      const CollectionConfig &owning = (*m_collections)[owningIndex];
      if (!owning.videoDirectory.trimmed().isEmpty()) {
        videoDirectory = owning.videoDirectory;
        videoExpansionName = owning.name;
      } else if (videoDirectory.trimmed().isEmpty() && m_collections) {
        videoDirectory = CollectionUtils::resolveVideoDirectory(owningIndex, *m_collections);
        videoExpansionName = owning.name;
      }
    }
  }
  if (videoDirectory.trimmed().isEmpty()) {
    m_coverFlowWidget->setVideoPathForIndex(visualIndex, QString());
    return;
  }
  videoDirectory = PathUtils::validateAndExpandPath(videoDirectory, videoExpansionName);
  const QString videoPath = VideoUtils::findVideoForFile(fullPath, videoDirectory);
  m_coverFlowWidget->setVideoPathForIndex(visualIndex, videoPath);
}

void ScrollManager::resolveAndPushCoverFlowGallery(int visualIndex) {
  if (!m_coverFlowWidget || !m_dataManager) {
    return;
  }
  const bool filtered = m_filterManager && m_filterManager->isFiltered();
  const int actualIndex = filtered ? m_filterManager->getActualIndex(visualIndex) : visualIndex;
  // Subcollection / virtual-folder cards have no per-item artwork variants
  // — clear the gallery so the toolbar disappears for those entries.
  if (actualIndex < 0 || !m_dataManager->isMediaIndex(actualIndex)) {
    m_coverFlowWidget->setGalleryForIndex(visualIndex, {});
    return;
  }
  const int mediaIdx = m_dataManager->mediaIndexFromActual(actualIndex);
  const QString rawEntry = m_dataManager->rawFilePath(mediaIdx);
  const QString fullPath =
      m_databaseManager ? m_databaseManager->resolveFilePath(rawEntry, m_context) : rawEntry;
  if (fullPath.isEmpty()) {
    m_coverFlowWidget->setGalleryForIndex(visualIndex, {});
    return;
  }

  // Resolve owner-aware directories so showAllSubcollectionItems sees the
  // child collection's artwork / video trees rather than the parent's.
  QString artworkDirectory = m_context.config.artworkDirectory;
  QString videoDirectory = m_context.config.videoDirectory;
  QString artworkExpansionName = m_context.config.name;
  QString videoExpansionName = m_context.config.name;
  QString collectionUuid;
  if (m_databaseManager) {
    const int owningIndex = m_databaseManager->getCollectionIndexForFile(fullPath);
    if (owningIndex >= 0 && m_collections && owningIndex < m_collections->size()) {
      const CollectionConfig &owning = (*m_collections)[owningIndex];
      if (!owning.artworkDirectory.trimmed().isEmpty()) {
        artworkDirectory = owning.artworkDirectory;
        artworkExpansionName = owning.name;
      } else if (artworkDirectory.trimmed().isEmpty() && m_collections) {
        artworkDirectory = CollectionUtils::resolveArtworkDirectory(owningIndex, *m_collections);
        artworkExpansionName = owning.name;
      }
      if (!owning.videoDirectory.trimmed().isEmpty()) {
        videoDirectory = owning.videoDirectory;
        videoExpansionName = owning.name;
      } else if (videoDirectory.trimmed().isEmpty() && m_collections) {
        videoDirectory = CollectionUtils::resolveVideoDirectory(owningIndex, *m_collections);
        videoExpansionName = owning.name;
      }
      const QString owningMediaDir =
          PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
      collectionUuid = CollectionUtils::computeCollectionUuid(owning.name, owningMediaDir);
    }
  }
  if (!artworkDirectory.trimmed().isEmpty()) {
    artworkDirectory = PathUtils::validateAndExpandPath(artworkDirectory, artworkExpansionName);
  }
  if (!videoDirectory.trimmed().isEmpty()) {
    videoDirectory = PathUtils::validateAndExpandPath(videoDirectory, videoExpansionName);
  }

  const QString baseName = QFileInfo(fullPath).completeBaseName();

  // Build the same {standard then custom} ordering the sidebar uses, with
  // per-item DB overrides taking precedence over auto-discovered subdir
  // layouts. Custom types only resolve when the user has explicitly set
  // an override row.
  QHash<QString, QString> overridesByType;
  QStringList customOrder;
  if (m_databaseManager && !collectionUuid.isEmpty()) {
    const auto rows = m_databaseManager->loadItemArtwork(collectionUuid, fullPath);
    for (const auto &row : rows) {
      overridesByType.insert(row.artworkType, row.manualPath);
      if (!ItemArtworkStore::isStandardType(row.artworkType)) {
        customOrder.append(row.artworkType);
      }
    }
  }

  QList<CoverFlowGalleryEntry> entries;
  // Track resolved paths so the standard-type pass doesn't append a tile
  // pointing at the same file the primary artwork already does.
  QSet<QString> seenPaths;
  auto pushEntry = [&](const QString &type, const QString &label) {
    const QString resolved = ItemArtworkStore::resolveArtworkPath(overridesByType.value(type),
                                                                  baseName, artworkDirectory, type);
    if (!resolved.isEmpty() && !seenPaths.contains(resolved)) {
      entries.append({label, resolved, /*isVideo=*/false});
      seenPaths.insert(resolved);
    }
  };

  // Lead with the carousel's primary artwork — the file picked up by
  // ArtworkUtils::findArtworkForFile in the flat artwork directory. That
  // image is what every other card in the carousel renders by default,
  // so it deserves a slot in the gallery so the user can return to it
  // after browsing variants.
  const QString primaryArtwork = resolveCardArtworkPath(fullPath, m_context, m_databaseManager);
  if (!primaryArtwork.isEmpty()) {
    entries.append({QObject::tr("Cover"), primaryArtwork, /*isVideo=*/false});
    seenPaths.insert(primaryArtwork);
  }

  for (const QString &type : ItemArtworkStore::standardTypes()) {
    pushEntry(type, ItemArtworkStore::standardTypeDisplayName(type));
  }
  for (const QString &type : customOrder) {
    pushEntry(type, type);
  }
  // Prepend the preview video so the gallery follows the video-first
  // ordering the rest of the preview flow uses (matches sidebar).
  if (!videoDirectory.isEmpty()) {
    const QString videoPath = VideoUtils::findVideoForFile(fullPath, videoDirectory);
    if (!videoPath.isEmpty()) {
      entries.prepend({QObject::tr("Video"), videoPath, /*isVideo=*/true});
    }
  }

  m_coverFlowWidget->setGalleryForIndex(visualIndex, entries);
}

void ScrollManager::rebuildCoverFlowCards() {
  if (!m_coverFlowWidget || !m_dataManager) {
    return;
  }
  // Walk the same visual-index space the rest of the system uses: when a
  // filter is active (search text, type filter, hideMissingArtwork, etc.)
  // selection-side code addresses items by *filtered* index, so the
  // carousel must mirror that ordering — otherwise card[N] and selection N
  // refer to different items and most of the carousel is unreachable.
  const bool filtered = m_filterManager && m_filterManager->isFiltered();
  const int total = filtered ? m_filterManager->filteredCount() : m_dataManager->totalItemCount();
  QList<CoverFlowCardData> cards;
  cards.reserve(total);

  for (int visualIndex = 0; visualIndex < total; ++visualIndex) {
    const int actualIndex = filtered ? m_filterManager->getActualIndex(visualIndex) : visualIndex;
    CoverFlowCardData card;
    if (actualIndex < 0) {
      cards.append(card);
      continue;
    }
    if (m_dataManager->isSubcollectionIndex(actualIndex)) {
      int sub = m_dataManager->subcollectionIndexFromActual(actualIndex);
      if (m_collections && sub >= 0 && sub < m_collections->size()) {
        const auto &subCfg = m_collections->at(sub);
        card.title = subCfg.name;
        card.artworkPath = subCfg.collectionIcon;
      }
    } else if (m_dataManager->isVirtualFolderIndex(actualIndex)) {
      const QString folder = m_dataManager->virtualFolderFromActual(actualIndex);
      card.title = QFileInfo(folder).fileName();
      // Virtual folders fall back to placeholder artwork.
    } else {
      const int mediaIdx = m_dataManager->mediaIndexFromActual(actualIndex);
      const QString rawEntry = m_dataManager->rawFilePath(mediaIdx);
      // Convert the raw entry (which may be a bare filename relative to the
      // collection's media directory) into a full absolute path so the
      // subfolder-mirroring artwork lookup has something to compute a
      // relative directory from. Without this step, raw entries with
      // subdirectories — and any collection whose artworkDir mirrors its
      // mediaDir tree — drop straight to placeholder.
      const QString fullPath =
          m_databaseManager ? m_databaseManager->resolveFilePath(rawEntry, m_context) : rawEntry;
      const QString fileName = QFileInfo(fullPath).fileName();
      card.title = m_dataManager->fileNames().value(fullPath.isEmpty() ? rawEntry : fullPath,
                                                    QFileInfo(fileName).completeBaseName());
      card.artworkPath = resolveCardArtworkPath(fullPath, m_context, m_databaseManager);
    }
    cards.append(card);
  }

  m_coverFlowWidget->setCards(cards);
}
