// Controls metadata sidebar visibility, positioning, and content updates.
#include "sidebarmanager.h"
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "itemwidget.h"
#include "metadatasidebar.h"
#include "settingsmanager.h"
#include "timerutils.h"
#include "uiconstants.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QTimer>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcSidebarManager, "kartend.sidebarmanager")
#define debugLog(msg) do { if (lcSidebarManager().isDebugEnabled()) { qCDebug(lcSidebarManager) << msg; } } while (0)

// SidebarManagerSetup getter definitions
SETUP_GETTER_DEF_SAME(SidebarManagerSetup, MetadataSidebar*, Sidebar, sidebar)
SETUP_GETTER_DEF_SAME(SidebarManagerSetup, QWidget*, ItemsPage, itemsPage)
SETUP_GETTER_DEF(SidebarManagerSetup, QScrollArea*, ScrollArea, scrollArea, itemScrollArea)
SETUP_GETTER_DEF_SAME(SidebarManagerSetup, SettingsManager*, SettingsManager, settingsManager)
SETUP_GETTER_DEF_SAME(SidebarManagerSetup, ArtworkManager*, ArtworkManager, artworkManager)
SETUP_GETTER_DEF_SAME(SidebarManagerSetup, QList<CollectionConfig>*, Collections, collections)

SidebarManager::SidebarManager(QObject *parent)
    : QObject(parent), m_MetadataSidebar(nullptr), m_itemsPage(nullptr),
      m_mainHorizontalLayout(nullptr), m_itemScrollArea(nullptr),
      m_currentCollectionIndex(-1) {}

void SidebarManager::setupReferences(const SidebarManagerSetup &setup) {
  m_MetadataSidebar = setup.getSidebar();
  m_itemsPage = setup.getItemsPage();
  m_mainHorizontalLayout = setup.mainLayout;
  m_itemScrollArea = setup.getScrollArea();
  m_settingsManager = setup.getSettingsManager();
  m_artworkManager = setup.getArtworkManager();
  m_collections = setup.getCollections();
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

  m_MetadataSidebar->setMetadata(filePath, itemName);
}

void SidebarManager::applySidebarStateForCollection(int collectionIndex) {
  if (!m_collections || collectionIndex < 0 ||
      collectionIndex >= m_collections->size()) {
    return;
  }

  m_currentCollectionIndex = collectionIndex;
  const CollectionConfig &collection = (*m_collections)[collectionIndex];
  m_sidebarVisible = collection.sidebarVisible;

  updateSidebarLayout(collectionIndex);
  emit sidebarVisibilityChanged(m_sidebarVisible);
}

void SidebarManager::setupSidebar() { m_sidebarVisible = false; }

void SidebarManager::positionSidebarOverlay() {
  if (!m_MetadataSidebar || !m_itemsPage) {
    return;
  }

  const int sidebarMargin = UIConstants::Sidebar::MARGIN;
  int sidebarWidth = m_MetadataSidebar->width() > 0
                         ? m_MetadataSidebar->width()
                         : UIConstants::Sidebar::MAX_WIDTH;

  QRect viewportRectInItems;
  if (m_itemScrollArea && m_itemScrollArea->viewport()) {
    const QPoint topLeft =
        m_itemScrollArea->viewport()->mapTo(m_itemsPage, QPoint(0, 0));
    viewportRectInItems = QRect(topLeft, m_itemScrollArea->viewport()->size());
  } else {
    viewportRectInItems = m_itemsPage->rect();
  }

  const int sidebarX = viewportRectInItems.left() +
                       viewportRectInItems.width() - sidebarWidth -
                       sidebarMargin;
  const int sidebarY = viewportRectInItems.top() + sidebarMargin;
  const int height =
      qMax(0, viewportRectInItems.height() - (sidebarMargin * 2));

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
    const CollectionConfig &collection =
        (*m_collections)[currentCollectionIndex];
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

      if (m_itemScrollArea &&
          m_itemScrollArea->viewport()) {
        QRect viewportRect = m_itemScrollArea->viewport()->rect();
        QPoint topLeft =
            m_itemScrollArea->viewport()->mapTo(m_itemsPage, QPoint(0, 0));

        int overlaySidebarWidth = UIConstants::Sidebar::MAX_WIDTH;
        m_MetadataSidebar->setFixedWidth(overlaySidebarWidth);

        int overlayX = topLeft.x() + viewportRect.width() -
                       overlaySidebarWidth - UIConstants::Sidebar::MARGIN;
        int overlayY = topLeft.y() + UIConstants::Sidebar::MARGIN;
        int height = viewportRect.height() - (UIConstants::Sidebar::MARGIN * 2);

        m_MetadataSidebar->setGeometry(overlayX, overlayY, overlaySidebarWidth,
                                       height);
      }

      m_MetadataSidebar->setVisible(true);
      m_MetadataSidebar->raise();
    } else {
      int viewportWidth = m_itemScrollArea->viewport()->width();
      int desired = viewportWidth / 4;
      int sidebarWidth = qMax(UIConstants::Sidebar::MIN_WIDTH,
                              qMin(UIConstants::Sidebar::MAX_WIDTH, desired));
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

  bool isNowInLayout =
      (m_mainHorizontalLayout->indexOf(m_MetadataSidebar) != -1);
  if (wasInLayout != isNowInLayout) {
    emit sidebarVisibilityChanged(m_sidebarVisible);
  }

  if (currentCollectionIndex >= 0) {
    saveSidebarStateForCollection(currentCollectionIndex, m_sidebarVisible);
  }

  if (m_artworkManager) {
    if (auto *timerCoordinator =
            m_artworkManager->getTimerCoordinator()) {
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
void SidebarManager::saveSidebarStateForCollection(int collectionIndex,
                                                   bool visible) {
  if (!m_collections || collectionIndex < 0 ||
      collectionIndex >= m_collections->size()) {
    return;
  }
  (*m_collections)[collectionIndex].sidebarVisible = visible;
  if (m_settingsManager) {
    m_settingsManager->saveCollections(*m_collections);
  }
}

// Persists the sidebar visibility state by collection name, forwarding to
// index-based save
void SidebarManager::saveSidebarStateForCollection(
    const QString &collectionName, bool visible) {
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