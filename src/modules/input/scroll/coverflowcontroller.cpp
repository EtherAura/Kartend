// Drives the CoverFlow ViewType. Extracted from ScrollManager
// (scrollmanagercoverflow.cpp).
//
// CoverFlowWidget lives as a sibling of gridContainer in the items page
// scroll-area layout. When the active collection's view type is CoverFlow we
// hide gridContainer + the scrollbars and show the cover-flow widget; when
// the user switches back to Grid/List we reverse it. The widget owns its
// own selection animation and artwork loading, so the only state pushed in
// is the card list and the canonical selected index — selection changes from
// user input round-trip back through SelectionManager via the
// selectItemByIndex signal so sidebar/restore/persistence stay coherent.

#include "coverflowcontroller.h"

#include "applicationcontext.h"
#include "artworkutils.h"
#include "collectionutils.h"
#include "coverflowwidget.h"
#include "filtermanager.h"
#include "idatabasemanager.h"
#include "itemartwork.h"
#include "pathutils.h"
#include "scrolldatamanager.h"
#include "textzoom.h"
#include "timerutils.h"
#include "uiconstants/scroll.h"
#include "videoutils.h"

#include <QBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QScrollArea>
#include <QScrollBar>

CoverFlowController::CoverFlowController(QObject *parent) : QObject(parent) {
  // Resolve debouncer — coalesces per-tick DB + FS lookups during a wheel
  // sweep across the carousel. The cheap parts of a selection update
  // (overlay state, glide animation) still run synchronously in
  // onSelectionChanged; only the per-item video + gallery resolution waits
  // for the selection to settle.
  m_resolveDebouncer =
      new TimerUtils::DebouncedTimer(UIConstants::Scroll::COVER_FLOW_RESOLVE_DEBOUNCE_MS, this);
  connect(m_resolveDebouncer, &TimerUtils::DebouncedTimer::triggered, this, [this]() {
    if (m_pendingVisualIndex < 0) {
      return;
    }
    const int idx = m_pendingVisualIndex;
    m_pendingVisualIndex = -1;
    // Re-gate on isActive() — the user may have switched view type during
    // the debounce window. The resolve fns also guard, but bailing here
    // avoids the redundant work.
    if (!isActive() || !m_widget) {
      return;
    }
    resolveAndPushVideo(idx);
    resolveAndPushGallery(idx);
  });
}

void CoverFlowController::setupReferences(const CoverFlowControllerSetup &setup) {
  m_ctx = setup.ctx;
  m_mediaScrollArea = setup.mediaScrollArea;
  m_gridContainer = setup.gridContainer;
  m_context = setup.context;
  m_collections = setup.collections;
  m_dataManager = setup.dataManager;
}

// Kartend-yeik: route FilterManager reads through ctx instead of holding a
// cached setup-struct pointer. ctx is seeded once in initializeAppContext;
// every accessor below is null-safe so the controller still no-ops cleanly
// when the filter isn't wired (headless smoke tests, early shutdown).
FilterManager *CoverFlowController::filterMgr() const {
  return m_ctx ? m_ctx->filterManager() : nullptr;
}

bool CoverFlowController::isActive() const {
  return m_context && m_context->config.viewType == ViewType::CoverFlow;
}

void CoverFlowController::ensureWidget() {
  if (m_widget) {
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
  m_widget = new CoverFlowWidget(contentParent);
  m_widget->hide();

  // Insert into the content widget's layout right before the metadata
  // sidebar, so the carousel takes the same horizontal slot the scroll
  // area otherwise occupies. The scroll area is still in the layout but
  // hidden when cover flow is active.
  if (auto *layout = qobject_cast<QBoxLayout *>(contentParent->layout())) {
    int idx = layout->indexOf(m_mediaScrollArea);
    if (idx >= 0) {
      layout->insertWidget(idx + 1, m_widget);
    } else {
      layout->addWidget(m_widget);
    }
  }

  // Selection requested by user input → run through the existing selection
  // pipeline so sidebar/restore/persistence stay coherent. The canonical
  // index then comes back to us via onSelectionChanged().
  connect(m_widget, &CoverFlowWidget::selectionChangeRequested, this,
          [this](int idx) { emit selectItemByIndex(idx); });

  // Activation: subcollections and virtual folders use their own navigation
  // paths; everything else is a media item launch routed through MainWindow.
  connect(m_widget, &CoverFlowWidget::itemActivated, this, [this](int idx) {
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
    emit itemActivated(idx);
  });
}

void CoverFlowController::applyConfig() {
  if (!m_widget || !m_context) {
    return;
  }
  const auto &cfg = m_context->config;
  m_widget->setCornerRadius(cfg.gridLayout.cornerRadius);
  m_widget->setTileColor(cfg.background.tileColor);
  m_widget->setSelectionColor(cfg.background.selectionColor);
  m_widget->setBackgroundColor(cfg.background.backgroundColor);
  m_widget->setHideTitles(cfg.hideTitles);
  // scale the cover-flow caption alongside grid/list items.
  m_widget->setFontSize(TextZoom::zoomedFontSize(cfg.gridLayout.fontSize));
  m_widget->setFontFamily(cfg.customFontFamily);
}

void CoverFlowController::applyVisibility() {
  const bool active = isActive();
  if (active) {
    ensureWidget();
  }
  if (m_widget) {
    m_widget->setVisible(active);
    if (active) {
      m_widget->raise();
      m_widget->update();
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
      m_mediaScrollArea->setHorizontalScrollBarPolicy(
          m_context && m_context->config.gridLayout.hideHorizontalScrollbar
              ? Qt::ScrollBarAlwaysOff
              : Qt::ScrollBarAsNeeded);
    }
  }
  if (active && m_widget) {
    m_widget->setFocus();
    // Pre-resolve the video and gallery for the currently centered card
    // so the very first middle-click can play it without a one-cycle
    // delay waiting for selectionChanged → resolve* to fire.
    const int initialIdx = m_widget->selectedIndex();
    resolveAndPushVideo(initialIdx);
    resolveAndPushGallery(initialIdx);
  }
  // Notify listeners (MainWindow → DetailsPaneManager) so the sidebar can yield
  // viewport space to the carousel without overwriting the persisted
  // per-collection sidebarVisible preference.
  emit activeChanged(active);
}

void CoverFlowController::rebuildCardsIfActive() {
  if (m_widget && isActive()) {
    rebuildCards();
  }
}

void CoverFlowController::refreshForViewTypeChange() {
  // cover-flow uses a parallel widget tree; keep its config, card list, and
  // visibility in sync with the grid's. ensureWidget() is idempotent so this
  // can run on every transition.
  ensureWidget();
  applyConfig();
  rebuildCards();
  applyVisibility();
}

void CoverFlowController::onSelectionChanged(int selectedIndex) {
  // keep the cover-flow widget in sync with the canonical selection —
  // animate the glide unless we're not actually displayed yet.
  if (!m_widget || !isActive()) {
    return;
  }
  m_widget->setSelectedIndex(selectedIndex, true);
  // Lazy video-path + gallery resolution: scanning the video directory and
  // loading per-item artwork rows for every one of N items at rebuild time
  // freezes the UI for large collections, so the per-item lookup happens
  // here on selection landing. Debounced so a wheel sweep across the
  // carousel only triggers one DB+FS pass at the trailing edge instead of
  // one per ~10ms tick.
  m_pendingVisualIndex = selectedIndex;
  if (m_resolveDebouncer) {
    m_resolveDebouncer->trigger();
  } else {
    // Debouncer not constructed yet (very-early calls during setup). Fall
    // back to synchronous resolution so behaviour matches the pre-debounce
    // path during init.
    resolveAndPushVideo(selectedIndex);
    resolveAndPushGallery(selectedIndex);
  }
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
// cache populates (rebuildCards runs on each receiveItemsRange).
static QString resolveCardArtworkPath(const QString &fullPath, const CollectionContext &context,
                                      IDatabaseManager *db) {
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
      context.config.folderBrowsing.includeArtworkSubfolders ||
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

void CoverFlowController::resolveAndPushVideo(int visualIndex) {
  if (!m_widget || !m_dataManager) {
    return;
  }
  IDatabaseManager *db = m_ctx ? m_ctx->databaseManager() : nullptr;
  // Only media items have preview videos — subcollection / virtual-folder
  // cards never carry one.
  const bool filtered = filterMgr() && filterMgr()->isFiltered();
  const int actualIndex = filtered ? filterMgr()->getActualIndex(visualIndex) : visualIndex;
  if (actualIndex < 0 || !m_dataManager->isMediaIndex(actualIndex)) {
    m_widget->setVideoPathForIndex(visualIndex, QString());
    return;
  }
  const int mediaIdx = m_dataManager->mediaIndexFromActual(actualIndex);
  const QString rawEntry = m_dataManager->rawFilePath(mediaIdx);
  const QString fullPath = db ? db->resolveFilePath(rawEntry, *m_context) : rawEntry;
  if (fullPath.isEmpty()) {
    m_widget->setVideoPathForIndex(visualIndex, QString());
    return;
  }
  // Mirror DetailsPaneManager's owner-aware resolution so showAllSubcollectionItems
  // collections find videos stored under the child collection's videoDirectory
  // even though the active context is the parent.
  QString videoDirectory = m_context->config.videoDirectory;
  QString videoExpansionName = m_context->config.name;
  if (db) {
    const int owningIndex = db->getCollectionIndexForFile(fullPath);
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
    m_widget->setVideoPathForIndex(visualIndex, QString());
    return;
  }
  videoDirectory = PathUtils::validateAndExpandPath(videoDirectory, videoExpansionName);
  const QString videoPath = VideoUtils::findVideoForFile(fullPath, videoDirectory);
  m_widget->setVideoPathForIndex(visualIndex, videoPath);
}

void CoverFlowController::resolveAndPushGallery(int visualIndex) {
  if (!m_widget || !m_dataManager) {
    return;
  }
  IDatabaseManager *db = m_ctx ? m_ctx->databaseManager() : nullptr;
  const bool filtered = filterMgr() && filterMgr()->isFiltered();
  const int actualIndex = filtered ? filterMgr()->getActualIndex(visualIndex) : visualIndex;
  // Subcollection / virtual-folder cards have no per-item artwork variants
  // — clear the gallery so the toolbar disappears for those entries.
  if (actualIndex < 0 || !m_dataManager->isMediaIndex(actualIndex)) {
    m_widget->setGalleryForIndex(visualIndex, {});
    return;
  }
  const int mediaIdx = m_dataManager->mediaIndexFromActual(actualIndex);
  const QString rawEntry = m_dataManager->rawFilePath(mediaIdx);
  const QString fullPath = db ? db->resolveFilePath(rawEntry, *m_context) : rawEntry;
  if (fullPath.isEmpty()) {
    m_widget->setGalleryForIndex(visualIndex, {});
    return;
  }

  // Resolve owner-aware directories so showAllSubcollectionItems sees the
  // child collection's artwork / video trees rather than the parent's.
  QString artworkDirectory = m_context->config.artworkDirectory;
  QString videoDirectory = m_context->config.videoDirectory;
  QString artworkExpansionName = m_context->config.name;
  QString videoExpansionName = m_context->config.name;
  QString collectionUuid;
  if (db) {
    const int owningIndex = db->getCollectionIndexForFile(fullPath);
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
  if (db && !collectionUuid.isEmpty()) {
    const auto rows = db->loadItemArtwork(collectionUuid, fullPath);
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
  const QString primaryArtwork = resolveCardArtworkPath(fullPath, *m_context, db);
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

  m_widget->setGalleryForIndex(visualIndex, entries);
}

void CoverFlowController::rebuildCards() {
  if (!m_widget || !m_dataManager) {
    return;
  }
  IDatabaseManager *db = m_ctx ? m_ctx->databaseManager() : nullptr;
  // Walk the same visual-index space the rest of the system uses: when a
  // filter is active (search text, type filter, hideMissingArtwork, etc.)
  // selection-side code addresses items by *filtered* index, so the
  // carousel must mirror that ordering — otherwise card[N] and selection N
  // refer to different items and most of the carousel is unreachable.
  const bool filtered = filterMgr() && filterMgr()->isFiltered();
  const int total = filtered ? filterMgr()->filteredCount() : m_dataManager->totalItemCount();
  QList<CoverFlowCardData> cards;
  cards.reserve(total);

  for (int visualIndex = 0; visualIndex < total; ++visualIndex) {
    const int actualIndex = filtered ? filterMgr()->getActualIndex(visualIndex) : visualIndex;
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
      const QString fullPath = db ? db->resolveFilePath(rawEntry, *m_context) : rawEntry;
      const QString fileName = QFileInfo(fullPath).fileName();
      card.title = m_dataManager->fileNames().value(fullPath.isEmpty() ? rawEntry : fullPath,
                                                    QFileInfo(fileName).completeBaseName());
      card.artworkPath = resolveCardArtworkPath(fullPath, *m_context, db);
    }
    cards.append(card);
  }

  m_widget->setCards(cards);
}
