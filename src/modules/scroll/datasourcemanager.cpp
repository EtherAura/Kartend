// DataSourceManager: owns FilterManager, ScrollDataManager,
// PreSearchStateManager, and SearchLoadingOverlay. Extracted from
// ScrollManager (Kartend-gg2).
#include "datasourcemanager.h"

#include "filtermanager.h"
#include "presearchstatemanager.h"
#include "scrolldatamanager.h"
#include "searchloadingoverlay.h"

#include <QWidget>

DataSourceManager::DataSourceManager(QObject *parent)
    : QObject(parent),
      m_filterManager(std::make_unique<FilterManager>(this)),
      m_dataManager(std::make_unique<ScrollDataManager>(this)),
      m_preSearchStateManager(std::make_unique<PreSearchStateManager>(this)),
      m_searchLoadingOverlay(std::make_unique<SearchLoadingOverlay>(this)) {
  connect(m_filterManager.get(), &FilterManager::filterChanged, this,
          &DataSourceManager::filterChanged);
}

DataSourceManager::~DataSourceManager() = default;

void DataSourceManager::setDatabaseManager(DatabaseManager *manager) {
  if (m_filterManager) {
    m_filterManager->setDatabaseManager(manager);
  }
}

void DataSourceManager::setSearchOverlayParent(QWidget *parent) {
  if (m_searchLoadingOverlay && parent) {
    m_searchLoadingOverlay->setParentWidget(parent);
  }
}

void DataSourceManager::showSearchLoadingOverlay() {
  if (m_searchLoadingOverlay) {
    m_searchLoadingOverlay->show();
  }
}

void DataSourceManager::hideSearchLoadingOverlay() {
  if (m_searchLoadingOverlay) {
    m_searchLoadingOverlay->hide();
  }
}

bool DataSourceManager::hasPreSearchState() const {
  return m_preSearchStateManager && m_preSearchStateManager->hasSavedState();
}

int DataSourceManager::getFilteredIndex(int visualIndex) const {
  if (!m_filterManager || !m_filterManager->isFiltered()) {
    return visualIndex;
  }
  return m_filterManager->getActualIndex(visualIndex);
}
