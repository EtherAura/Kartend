// Controls metadata sidebar visibility, positioning, and content updates.
#include "sidebarmanager.h"
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "itemartwork.h"
#include "itemartworklinksdialog.h"
#include "itemmetadata.h"
#include "itemwidget.h"
#include "metadatasidebar.h"
#include "pathutils.h"
#include "settingsmanager.h"
#include "timerutils.h"
#include "uiconstants.h"
#include "videoutils.h"
#include <QApplication>
#include <QFileInfo>
#include <QHash>
#include <QHBoxLayout>
#include <QList>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QString>
#include <QTimer>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcSidebarManager, "kartend.sidebarmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcSidebarManager().isDebugEnabled()) {                                                     \
      qCDebug(lcSidebarManager) << msg;                                                            \
    }                                                                                              \
  } while (0)

// SidebarManagerSetup getter definitions
SETUP_GETTER_DEF_UI_SAME(SidebarManagerSetup, MetadataSidebar *, Sidebar, sidebar)
SETUP_GETTER_DEF_UI_SAME(SidebarManagerSetup, QWidget *, ItemsPage, itemsPage)
SETUP_GETTER_DEF_UI(SidebarManagerSetup, QScrollArea *, ScrollArea, scrollArea, itemScrollArea)
SETUP_GETTER_DEF_MGR_SAME(SidebarManagerSetup, SettingsManager *, SettingsManager, settingsManager)
SETUP_GETTER_DEF_MGR_SAME(SidebarManagerSetup, ArtworkManager *, ArtworkManager, artworkManager)
SETUP_GETTER_DEF_MGR_SAME(SidebarManagerSetup, DatabaseManager *, DatabaseManager, databaseManager)
SETUP_GETTER_DEF_COL_SAME(SidebarManagerSetup, QList<CollectionConfig> *, Collections, collections)

SidebarManager::SidebarManager(QObject *parent)
    : QObject(parent), m_MetadataSidebar(nullptr), m_itemsPage(nullptr),
      m_mainHorizontalLayout(nullptr), m_itemScrollArea(nullptr), m_currentCollectionIndex(-1) {}

void SidebarManager::setupReferences(const SidebarManagerSetup &setup) {
  m_MetadataSidebar = setup.getSidebar();
  m_itemsPage = setup.getItemsPage();
  m_mainHorizontalLayout = setup.mainLayout;
  m_itemScrollArea = setup.getScrollArea();
  m_settingsManager = setup.getSettingsManager();
  m_artworkManager = setup.getArtworkManager();
  m_databaseManager = setup.getDatabaseManager();
  m_collections = setup.getCollections();

  // Wire the per-item artwork-link editor (Kartend-53vk). The sidebar
  // widget itself has no item context, so the manager handles the dialog.
  if (m_MetadataSidebar) {
    connect(m_MetadataSidebar, &MetadataSidebar::editArtworkRequested, this,
            &SidebarManager::openArtworkLinksDialog);
  }
}

void SidebarManager::toggleSidebar() {
  if (!m_MetadataSidebar) {
    return;
  }

  m_sidebarVisible = !m_sidebarVisible;
  updateSidebarLayout(m_currentCollectionIndex);
  emit sidebarVisibilityChanged(m_sidebarVisible);
}

void SidebarManager::updateSidebarMetadata(ItemWidget *selectedItem) {
  if (!m_MetadataSidebar || !selectedItem) {
    if (m_MetadataSidebar) {
      m_MetadataSidebar->clearMetadata();
    }
    return;
  }

  QString filePath = selectedItem->getFilePath();
  QString itemName = selectedItem->getItemName();

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
  QString metaUuid;
  if (m_databaseManager) {
    const int owningIndex = m_databaseManager->getCollectionIndexForFile(filePath);
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

  m_MetadataSidebar->setMetadata(filePath, itemName, artworkDirectory, videoDirectory);

  // Extended metadata + manual file (Kartend-rx64 / Kartend-9jdv).
  ItemMetadataStore::ItemMetadata loadedMetadata;
  if (m_databaseManager && !metaUuid.isEmpty()) {
    loadedMetadata = m_databaseManager->loadItemMetadata(metaUuid, filePath);
  }
  if (m_databaseManager) {
    m_MetadataSidebar->setExtendedMetadata(loadedMetadata);
  }

  // Usage statistics (Kartend-7vi). Append play count / last played / time
  // played to the Details section. Loaded after setExtendedMetadata so the
  // section's row layout is already in place; setUsageStats only appends.
  if (m_databaseManager && !metaUuid.isEmpty()) {
    const auto usage = m_databaseManager->loadItemUsageStats(metaUuid, filePath);
    m_MetadataSidebar->setUsageStats(usage);
  }

  const QString baseName = QFileInfo(filePath).completeBaseName();
  const QString manualPath =
      ItemMetadataStore::resolveManualFile(loadedMetadata.manualPath, baseName, manualDirectory);
  m_MetadataSidebar->setManualFile(manualPath);

  // Build the artwork gallery (Kartend-un3l). For every standard artwork
  // type, prefer the per-item DB override, then fall back to the
  // {artworkDirectory}/{type}/{baseName}.{ext} subdirectory layout. Custom
  // (non-standard) types only resolve via a stored override. An empty list
  // hides the gallery section.
  QList<MetadataSidebar::GalleryEntry> galleryEntries;
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
  // the rest of the preview flow uses (Kartend-ljey). Auto-discovered via
  // VideoUtils against the owning collection's videoDirectory; per-item
  // overrides can be added later alongside the artwork override system.
  if (!videoDirectory.trimmed().isEmpty()) {
    const QString videoPath = VideoUtils::findVideoForFile(filePath, videoDirectory);
    if (!videoPath.isEmpty()) {
      galleryEntries.prepend({tr("Video"), videoPath, /*isVideo=*/true});
    }
  }

  m_MetadataSidebar->setArtworkGallery(galleryEntries);

  // Capture the resolved owner context so the artwork-link editor dialog
  // (Kartend-53vk) doesn't have to redo the showAllSubcollectionItems-aware
  // lookup. Only enable the edit affordance once we have a UUID — without
  // one we couldn't persist anything anyway.
  m_currentItemFilePath = filePath;
  m_currentItemName = itemName;
  m_currentItemUuid = metaUuid;
  m_currentItemArtworkDir = artworkDirectory;
  if (m_databaseManager) {
    m_currentItemOwningIndex = m_databaseManager->getCollectionIndexForFile(filePath);
  } else {
    m_currentItemOwningIndex = -1;
  }
  m_MetadataSidebar->setArtworkEditEnabled(!metaUuid.isEmpty());
}

void SidebarManager::applySidebarStateForCollection(int collectionIndex) {
  if (!m_collections || collectionIndex < 0 || collectionIndex >= m_collections->size()) {
    return;
  }

  m_currentCollectionIndex = collectionIndex;
  const CollectionConfig &collection = (*m_collections)[collectionIndex];
  m_sidebarVisible = collection.sidebarVisible;

  updateSidebarLayout(collectionIndex);
  emit sidebarVisibilityChanged(m_sidebarVisible);

  // Reposition overlay sidebar after layout is finalized - on startup, the
  // viewport geometry may not be fully set when this is first called, causing
  // the sidebar to overlap the scrollbar. Deferring ensures correct
  // positioning.
  if (m_sidebarVisible) {
    QTimer::singleShot(50, this, [this]() { positionSidebarOverlay(); });
  }
}

void SidebarManager::setupSidebar() {
  m_sidebarVisible = false;
}

void SidebarManager::positionSidebarOverlay() {
  if (!m_MetadataSidebar || !m_itemsPage) {
    return;
  }

  const int sidebarMargin = UIConstants::Sidebar::MARGIN;
  int sidebarWidth =
      m_MetadataSidebar->width() > 0 ? m_MetadataSidebar->width() : UIConstants::Sidebar::MAX_WIDTH;

  QRect viewportRectInItems;
  int scrollbarWidth = 0;
  if (m_itemScrollArea && m_itemScrollArea->viewport()) {
    const QPoint topLeft = m_itemScrollArea->viewport()->mapTo(m_itemsPage, QPoint(0, 0));
    viewportRectInItems = QRect(topLeft, m_itemScrollArea->viewport()->size());

    // Account for scrollbar width - the overlay scrollbar appears over the
    // viewport, so we need to offset the sidebar to avoid covering it.
    // Use sizeHint for consistent positioning even before scrollbar is visible.
    if (auto *vScrollBar = m_itemScrollArea->verticalScrollBar()) {
      static constexpr int DEFAULT_SCROLLBAR_WIDTH = 16;
      int barWidth = vScrollBar->sizeHint().width();
      scrollbarWidth = barWidth > 0 ? barWidth : DEFAULT_SCROLLBAR_WIDTH;
    }
  } else {
    viewportRectInItems = m_itemsPage->rect();
  }

  const int sidebarX = viewportRectInItems.left() + viewportRectInItems.width() - sidebarWidth -
                       sidebarMargin - scrollbarWidth;
  const int sidebarY = viewportRectInItems.top() + sidebarMargin;
  const int height = qMax(0, viewportRectInItems.height() - (sidebarMargin * 2));

  m_MetadataSidebar->setGeometry(sidebarX, sidebarY, sidebarWidth, height);
  m_MetadataSidebar->raise();
}

void SidebarManager::updateSidebarLayout(int currentCollectionIndex) {
  if (!m_MetadataSidebar || !m_mainHorizontalLayout) {
    return;
  }

  bool isFixedMode = false;
  if (m_collections && currentCollectionIndex >= 0 &&
      currentCollectionIndex < m_collections->size()) {
    const CollectionConfig &collection = (*m_collections)[currentCollectionIndex];
    isFixedMode = (collection.sidebarMode == SidebarMode::Expand);
  }

  bool wasInLayout = (m_mainHorizontalLayout->indexOf(m_MetadataSidebar) != -1);
  m_MetadataSidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  if (m_sidebarVisible) {
    if (isFixedMode) {
      m_MetadataSidebar->setParent(m_itemsPage);
      if (m_mainHorizontalLayout->indexOf(m_MetadataSidebar) != -1) {
        m_mainHorizontalLayout->removeWidget(m_MetadataSidebar);
      }

      int sidebarWidth = UIConstants::Sidebar::MAX_WIDTH;
      m_MetadataSidebar->setFixedWidth(sidebarWidth);

      // Use common positioning logic to ensure scrollbar offset is applied
      positionSidebarOverlay();
      m_MetadataSidebar->setVisible(true);
      m_MetadataSidebar->raise();
    } else {
      int viewportWidth = m_itemScrollArea->viewport()->width();
      int desired = viewportWidth / 4;
      int sidebarWidth =
          qMax(UIConstants::Sidebar::MIN_WIDTH, qMin(UIConstants::Sidebar::MAX_WIDTH, desired));
      sidebarWidth = qMax(UIConstants::Sidebar::MIN_WIDTH,
                          sidebarWidth - UIConstants::Sidebar::SCROLLBAR_OFFSET);
      m_MetadataSidebar->setParent(m_itemsPage);
      if (m_mainHorizontalLayout->indexOf(m_MetadataSidebar) != -1) {
        m_mainHorizontalLayout->removeWidget(m_MetadataSidebar);
      }
      m_MetadataSidebar->setFixedWidth(sidebarWidth);
      positionSidebarOverlay();
      m_MetadataSidebar->setVisible(true);
    }
  } else {
    m_MetadataSidebar->setVisible(false);
    if (m_mainHorizontalLayout->indexOf(m_MetadataSidebar) != -1) {
      m_mainHorizontalLayout->removeWidget(m_MetadataSidebar);
    }
  }

  bool isNowInLayout = (m_mainHorizontalLayout->indexOf(m_MetadataSidebar) != -1);
  if (wasInLayout != isNowInLayout) {
    emit sidebarVisibilityChanged(m_sidebarVisible);
  }

  if (currentCollectionIndex >= 0) {
    saveSidebarStateForCollection(currentCollectionIndex, m_sidebarVisible);
  }

  if (m_artworkManager) {
    if (auto *timerCoordinator = m_artworkManager->getTimerCoordinator()) {
      timerCoordinator->scheduleLayoutUpdate();
    }
  }

  // Delay sidebar layout changed signal to allow show/hide animation
  // to start before other components react to the layout change
  QTimer::singleShot(UIConstants::Sidebar::LAYOUT_NOTIFY_DELAY_MS, this,
                     [this]() { emit sidebarLayoutChanged(); });
}

auto SidebarManager::isSidebarVisible() const -> bool {
  return m_sidebarVisible;
}

// Persists the sidebar visibility state for a collection index and writes
// settings
void SidebarManager::saveSidebarStateForCollection(int collectionIndex, bool visible) {
  if (!m_collections || collectionIndex < 0 || collectionIndex >= m_collections->size()) {
    return;
  }
  (*m_collections)[collectionIndex].sidebarVisible = visible;
  if (m_settingsManager) {
    m_settingsManager->saveCollections(*m_collections);
  }
}

void SidebarManager::openArtworkLinksDialog() {
  if (!m_MetadataSidebar || !m_databaseManager || !m_collections) {
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

  ItemArtworkLinksDialog dialog(m_MetadataSidebar->window());
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
  if (m_MetadataSidebar && !m_currentItemUuid.isEmpty()) {
    QList<MetadataSidebar::GalleryEntry> galleryEntries;
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
    m_MetadataSidebar->setArtworkGallery(galleryEntries);
  }
}

// Persists the sidebar visibility state by collection name, forwarding to
// index-based save
void SidebarManager::saveSidebarStateForCollection(const QString &collectionName, bool visible) {
  if (!m_collections) {
    return;
  }
  int idx = -1;
  for (int i = 0; i < m_collections->size(); ++i) {
    if ((*m_collections)[i].name == collectionName) {
      idx = i;
      break;
    }
  }
  if (idx < 0) {
    return;
  }
  saveSidebarStateForCollection(idx, visible);
}