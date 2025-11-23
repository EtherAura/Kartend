#include "sidebarmanager.h"
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

SidebarManager::SidebarManager(QObject *parent)
    : QObject(parent), m_metadataSidebar(nullptr), m_itemsPage(nullptr),
      m_mainHorizontalLayout(nullptr), m_itemScrollArea(nullptr),
      m_currentCollectionIndex(-1) {}

void SidebarManager::setupReferences(metadataSidebar *sidebar,
                                     QWidget *itemsPage,
                                     QHBoxLayout *mainLayout,
                                     QScrollArea *scrollArea) {
  m_metadataSidebar = sidebar;
  m_itemsPage = itemsPage;
  m_mainHorizontalLayout = mainLayout;
  m_itemScrollArea = scrollArea;
}

void SidebarManager::setSettingsManager(SettingsManager *manager) {
  m_settingsManager = manager;
}

// Sets the collections pointer for sidebar state updates
void SidebarManager::setCollections(QList<CollectionConfig> *collections) {
  m_collections = collections;
}

void SidebarManager::toggleSidebar() {
  if (m_metadataSidebar == nullptr) {
    return;
  }

  m_sidebarVisible = !m_sidebarVisible;
  updateSidebarLayout(m_currentCollectionIndex);
  emit sidebarVisibilityChanged(m_sidebarVisible);
}

void SidebarManager::updateSidebarMetadata(MediaItemWidget *selectedItem) {
  if (m_metadataSidebar == nullptr || selectedItem == nullptr) {
    if (m_metadataSidebar != nullptr) {
      m_metadataSidebar->clearMetadata();
    }
    return;
  }

  QString filePath = selectedItem->getFilePath();
  QString itemName = selectedItem->getItemName();

  m_metadataSidebar->setmetadata(filePath, itemName);
}

void SidebarManager::applySidebarStateForCollection(int collectionIndex) {
  if (m_collections == nullptr || collectionIndex < 0 ||
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
  if (m_metadataSidebar == nullptr || m_itemsPage == nullptr) {
    return;
  }

  const int sidebarMargin = UIConstants::SIDEBAR_MARGIN;
  int sidebarWidth = m_metadataSidebar->width() > 0
                         ? m_metadataSidebar->width()
                         : UIConstants::SIDEBAR_MAX_WIDTH;

  QRect viewportRectInItems;
  if (m_itemScrollArea != nullptr && m_itemScrollArea->viewport() != nullptr) {
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

  m_metadataSidebar->setGeometry(sidebarX, sidebarY, sidebarWidth, height);
  m_metadataSidebar->raise();
}

void SidebarManager::updateSidebarLayout(int currentCollectionIndex) {
  if (m_metadataSidebar == nullptr || m_mainHorizontalLayout == nullptr) {
    return;
  }

  bool isFixedMode = false;
  if (m_collections != nullptr && currentCollectionIndex >= 0 &&
      currentCollectionIndex < m_collections->size()) {
    const CollectionConfig &collection =
        (*m_collections)[currentCollectionIndex];
    isFixedMode = (collection.sidebarMode == SidebarMode::Expand);
  }

  bool wasInLayout = (m_mainHorizontalLayout->indexOf(m_metadataSidebar) != -1);
  m_metadataSidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  if (m_sidebarVisible) {
    if (isFixedMode) {
      m_metadataSidebar->setParent(m_itemsPage);
      if (m_mainHorizontalLayout->indexOf(m_metadataSidebar) != -1) {
        m_mainHorizontalLayout->removeWidget(m_metadataSidebar);
      }

      int sidebarWidth = UIConstants::SIDEBAR_MAX_WIDTH;
      m_metadataSidebar->setFixedWidth(sidebarWidth);

      if (m_itemScrollArea != nullptr &&
          m_itemScrollArea->viewport() != nullptr) {
        QRect viewportRect = m_itemScrollArea->viewport()->rect();
        QPoint topLeft =
            m_itemScrollArea->viewport()->mapTo(m_itemsPage, QPoint(0, 0));

        int overlaySidebarWidth = UIConstants::SIDEBAR_MAX_WIDTH;
        m_metadataSidebar->setFixedWidth(overlaySidebarWidth);

        int overlayX = topLeft.x() + viewportRect.width() -
                       overlaySidebarWidth - UIConstants::SIDEBAR_MARGIN;
        int overlayY = topLeft.y() + UIConstants::SIDEBAR_MARGIN;
        int height = viewportRect.height() - (UIConstants::SIDEBAR_MARGIN * 2);

        m_metadataSidebar->setGeometry(overlayX, overlayY, overlaySidebarWidth,
                                       height);
      }

      m_metadataSidebar->setVisible(true);
      m_metadataSidebar->raise();
    } else {
      int viewportWidth = m_itemScrollArea->viewport()->width();
      int desired = viewportWidth / 4;
      int sidebarWidth = qMax(UIConstants::SIDEBAR_MIN_WIDTH,
                              qMin(UIConstants::SIDEBAR_MAX_WIDTH, desired));
      sidebarWidth = qMax(UIConstants::SIDEBAR_MIN_WIDTH,
                          sidebarWidth - UIConstants::SIDEBAR_SCROLLBAR_OFFSET);
      m_metadataSidebar->setParent(m_itemsPage);
      if (m_mainHorizontalLayout->indexOf(m_metadataSidebar) != -1) {
        m_mainHorizontalLayout->removeWidget(m_metadataSidebar);
      }
      m_metadataSidebar->setFixedWidth(sidebarWidth);
      positionSidebarOverlay();
      m_metadataSidebar->setVisible(true);
    }
  } else {
    m_metadataSidebar->setVisible(false);
    if (m_mainHorizontalLayout->indexOf(m_metadataSidebar) != -1) {
      m_mainHorizontalLayout->removeWidget(m_metadataSidebar);
    }
  }

  bool isNowInLayout =
      (m_mainHorizontalLayout->indexOf(m_metadataSidebar) != -1);
  if (wasInLayout != isNowInLayout) {
    emit sidebarVisibilityChanged(m_sidebarVisible);
  }

  if (currentCollectionIndex >= 0) {
    saveSidebarStateForCollection(currentCollectionIndex, m_sidebarVisible);
  }

  if (auto *timerCoordinator =
          ArtworkManager::instance().getTimerCoordinator()) {
    timerCoordinator->scheduleLayoutUpdate();
  }

  QTimer::singleShot(UIConstants::SIDEBAR_LAYOUT_NOTIFY_DELAY_MS, this,
                     [this]() { emit sidebarLayoutChanged(); });
}

auto SidebarManager::isSidebarVisible() const -> bool {
  return m_sidebarVisible;
}

// Persists the sidebar visibility state for a collection index and writes
// settings
void SidebarManager::saveSidebarStateForCollection(int collectionIndex,
                                                   bool visible) {
  if (m_collections == nullptr || collectionIndex < 0 ||
      collectionIndex >= m_collections->size()) {
    return;
  }
  (*m_collections)[collectionIndex].sidebarVisible = visible;
  if (m_settingsManager != nullptr) {
    m_settingsManager->saveCollections(*m_collections);
  }
}

// Persists the sidebar visibility state by collection name, forwarding to
// index-based save
void SidebarManager::saveSidebarStateForCollection(
    const QString &collectionName, bool visible) {
  if (m_collections == nullptr) {
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