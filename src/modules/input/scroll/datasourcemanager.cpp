// DataSourceCoordinator: owns FilterManager, ScrollDataStore,
// PreSearchStateCache, and SearchLoadingOverlay. Extracted from
// ScrollManager.
#include "datasourcemanager.h"

#include "filtermanager.h"
#include "presearchstatemanager.h"
#include "scrolldatamanager.h"
#include "searchloadingoverlay.h"

#include <QWidget>

DataSourceCoordinator::DataSourceCoordinator(QObject *parent)
    : QObject(parent), m_filterManager(std::make_unique<FilterManager>(this)),
      m_dataManager(std::make_unique<ScrollDataStore>(this)),
      m_preSearchStateManager(std::make_unique<PreSearchStateCache>(this)),
      m_searchLoadingOverlay(std::make_unique<SearchLoadingOverlay>(this)) {
  connect(m_filterManager.get(), &FilterManager::filterChanged, this,
          &DataSourceCoordinator::filterChanged);
}

DataSourceCoordinator::~DataSourceCoordinator() = default;

void DataSourceCoordinator::setApplicationContext(const ApplicationContext *ctx) {
  if (m_filterManager) {
    m_filterManager->setApplicationContext(ctx);
  }
}

void DataSourceCoordinator::setSearchOverlayParent(QWidget *parent) {
  if (m_searchLoadingOverlay && parent) {
    m_searchLoadingOverlay->setParentWidget(parent);
  }
}

void DataSourceCoordinator::showSearchLoadingOverlay() {
  if (m_searchLoadingOverlay) {
    m_searchLoadingOverlay->show();
  }
}

void DataSourceCoordinator::hideSearchLoadingOverlay() {
  if (m_searchLoadingOverlay) {
    m_searchLoadingOverlay->hide();
  }
}

bool DataSourceCoordinator::hasPreSearchState() const {
  return m_preSearchStateManager && m_preSearchStateManager->hasSavedState();
}

int DataSourceCoordinator::getFilteredIndex(int visualIndex) const {
  if (!m_filterManager || !m_filterManager->isFiltered()) {
    return visualIndex;
  }
  return m_filterManager->getActualIndex(visualIndex);
}
