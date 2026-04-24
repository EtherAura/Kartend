// Sibling TU: appearance/styling application for NavigationManager.
#include "navigationmanager.h"
#include "loggingcategories.h"
#include "artworkmanager.h"
#include "artworkutils.h"
#include "databasemanager.h"
#include "errordialog.h"
#include "interactionmanager.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "loadingoverlay.h"
#include "metadatasidebar.h"
#include "navigationstackmanager.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "selectionrestoremanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "sidebarmanager.h"
#include "timerutils.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QtGlobal>
#include <algorithm>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcNavigationManager)
#define debugLog(msg)                                                          \
  do {                                                                         \
    if (lcNavigationManager().isDebugEnabled()) {                              \
      qCDebug(lcNavigationManager) << msg;                                     \
    }                                                                          \
  } while (0)

auto NavigationManager::applyCollectionSettingsOnly(int collectionIndex)
    -> void {
  if (collectionIndex < 0 || collectionIndex >= (*m_collections).size()) {
    return;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];

  if (collection.gridWidth != m_scrollManager->getCurrentGridWidth()) {
    m_scrollManager->updateGridWidth(collection.gridWidth);
  }

  SettingsUtils::applyHorizontalScrollbarSetting(
      m_itemScrollArea, collectionIndex, (*m_collections));
  SettingsUtils::applyVerticalScrollbarSetting(
      m_itemScrollArea, collectionIndex, (*m_collections));

  applyBackgroundForCollection(collectionIndex);
  applyPrimaryColorForCollection(collectionIndex);

  if (m_sidebarManager) {
    m_sidebarManager->applySidebarStateForCollection(collectionIndex);
  }
}

void NavigationManager::applyBackgroundForCollection(int collectionIndex) {
  if (!m_itemScrollArea || collectionIndex < 0 ||
      collectionIndex >= (*m_collections).size()) {
    return;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];
  QWidget *viewport = m_itemScrollArea->viewport();
  if (!viewport) {
    return;
  }

  QString styleSheet;
  if (collection.backgroundType == BackgroundType::Image &&
      !collection.backgroundImage.isEmpty()) {
    // Background image mode
    QString imagePath = collection.backgroundImage;
    // Escape backslashes for CSS
    imagePath.replace("\\", "/");
    styleSheet = QString("QWidget { "
                         "background-image: url(\"%1\"); "
                         "background-repeat: no-repeat; "
                         "background-position: center; "
                         "background-attachment: fixed; "
                         "}")
                     .arg(imagePath);
  } else if (!collection.backgroundColor.isEmpty()) {
    // Background color mode
    styleSheet = QString("QWidget { background-color: %1; }")
                     .arg(collection.backgroundColor);
  } else {
    // Clear any custom background (use system default)
    styleSheet.clear();
  }

  viewport->setStyleSheet(styleSheet);
}

void NavigationManager::applyPrimaryColorForCollection(int collectionIndex) {
  if (collectionIndex < 0 || collectionIndex >= (*m_collections).size()) {
    return;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];
  ItemWidget::setPrimaryColor(collection.primaryColor);
  ItemWidget::setTileColor(collection.tileColor);
  ItemWidget::setSelectionColor(collection.selectionColor);
  ItemWidget::setListRowColor(collection.listRowColor);
  ItemWidget::setListAltRowColor(collection.listAltRowColor);
  ItemWidget::setCustomFontFamily(collection.customFontFamily);

  bool hasPrimaryColor = !collection.primaryColor.isEmpty() &&
                         QColor::isValidColorName(collection.primaryColor);

  // Apply primary color to toolbar/top bar (exact color, not tinted)
  if (m_itemsTopBar) {
    QString toolbarStyle;
    if (hasPrimaryColor) {
      toolbarStyle = QString("QWidget#itemsTopBar { background-color: %1; }")
                         .arg(collection.primaryColor);
    }
    m_itemsTopBar->setStyleSheet(toolbarStyle);
  }

  // Apply primary color to menubar
  if (m_menubar) {
    QString menubarStyle;
    if (hasPrimaryColor) {
      menubarStyle = QString("QMenuBar { background-color: %1; }"
                             "QMenuBar::item { background-color: transparent; }"
                             "QMenuBar::item:selected { background-color: "
                             "rgba(255,255,255,0.2); }")
                         .arg(collection.primaryColor);
    }
    m_menubar->setStyleSheet(menubarStyle);
  }

  // Apply primary color to search bar background
  if (m_searchBar) {
    QString searchBarStyle;
    if (hasPrimaryColor) {
      // Tint the primary color slightly for the search bar background
      QColor baseColor(collection.primaryColor);
      QColor bgColor = baseColor.lighter(130);
      searchBarStyle = QString("QLineEdit { background-color: %1; border: 1px "
                               "solid %2; border-radius: 4px; padding: 4px; }"
                               "QLineEdit:focus { border-color: %3; }")
                           .arg(bgColor.name())
                           .arg(baseColor.darker(110).name())
                           .arg(baseColor.name());
    }
    m_searchBar->setStyleSheet(searchBarStyle);
  }
}

void NavigationManager::restoreSelectionForCurrentCollection() {
  if ((!parent()) || QApplication::closingDown()) {
    return;
  }
  if ((!m_scrollManager) || (!m_interactionManager)) {
    return;
  }
  int coll = (*m_currentCollectionIndex);
  if (coll < 0 || coll >= (*m_collections).size()) {
    return;
  }
  int total = m_scrollManager->getTotalItems();
  if (total <= 0) {
    return;
  }
  int desired = -1;
  if (m_settingsManager) {
    desired = m_settingsManager->getLastSelectedItem(coll);
  }
  if (desired < 0 || desired >= total) {
    desired = 0;
  }
  if (m_interactionManager->currentSelectedIndex() == desired) {
    return;
  }

  scheduleSelectionRestore(desired, UIConstants::Selection::RESTORE_STEPS,
                           UIConstants::Selection::RESTORE_STEP_DELAY_MS,
                           UIConstants::Selection::RESTORE_MAX_DELAY_MS);
}
void NavigationManager::persistCurrentSelection() {
  static const bool diagEnabled =
      qEnvironmentVariableIntValue("KARTEND_SEARCH_DIAG");
  if (diagEnabled) {
    qCDebug(lcSearchDiag) << "[NavigationManager] persistCurrentSelection: ENTRY";
  }
  if ((!m_interactionManager) || (!m_settingsManager) ||
      (!m_currentCollectionIndex) || (!m_collections)) {
    if (diagEnabled) {
      qCDebug(lcSearchDiag) << "[NavigationManager] persistCurrentSelection: "
                    "missing deps"
                 << "interaction=" << static_cast<bool>(m_interactionManager)
                 << "settings=" << static_cast<bool>(m_settingsManager)
                 << "collIndex=" << static_cast<bool>(m_currentCollectionIndex)
                 << "collections=" << static_cast<bool>(m_collections);
    }
    return;
  }
  int coll = *m_currentCollectionIndex;
  if (!CollectionUtils::isValidIndex(coll, m_collections)) {
    if (diagEnabled) {
      qCDebug(lcSearchDiag) << "[NavigationManager] persistCurrentSelection: "
                    "invalid collection index"
                 << coll;
    }
    return;
  }
  int sel = m_interactionManager->currentSelectedIndex();
  if (sel < 0) {
    if (diagEnabled) {
      qCDebug(lcSearchDiag) << "[NavigationManager] persistCurrentSelection: "
                    "no selection, caching viewport anyway";
    }
    // Still try to cache viewport even without selection for fast startup
  } else {
    m_settingsManager->setLastSelectedItem(coll, sel);
  }

  // Also cache the current viewport for instant startup
  if (m_scrollManager && m_sessionManager && m_generalSettings &&
      m_generalSettings->rememberSelection) {
    int startIndex = 0;
    int totalItems = 0;
    QStringList filePaths;
    QHash<QString, QString> fileNames;
    QHash<QString, QString> artworkPaths;

    if (m_scrollManager->getCurrentViewportForCache(
            startIndex, totalItems, filePaths, fileNames, artworkPaths)) {
      const CollectionConfig &cfg = (*m_collections)[coll];
      const QString collectionKey =
          CollectionUtils::hierarchicalNameFor(cfg, *m_collections);
      if (diagEnabled) {
        qCDebug(lcSearchDiag) << "[NavigationManager] "
                      "persistCurrentSelection: caching viewport for"
                   << collectionKey << "startIndex=" << startIndex
                   << "totalItems=" << totalItems
                   << "filePaths=" << filePaths.size();
      }
      m_sessionManager->setCachedViewport(collectionKey, startIndex, totalItems,
                                          filePaths, fileNames, artworkPaths);
    } else if (diagEnabled) {
      qCDebug(lcSearchDiag) << "[NavigationManager] persistCurrentSelection: "
                    "getCurrentViewportForCache returned false";
    }
  } else if (diagEnabled) {
    qCDebug(lcSearchDiag) << "[NavigationManager] persistCurrentSelection: "
                  "cannot cache viewport"
               << "scrollManager=" << static_cast<bool>(m_scrollManager)
               << "sessionManager=" << static_cast<bool>(m_sessionManager)
               << "generalSettings=" << static_cast<bool>(m_generalSettings)
               << "rememberSelection="
               << (m_generalSettings ? m_generalSettings->rememberSelection
                                     : false);
  }
}

void NavigationManager::applyUiPoliciesForCollection(int collectionIndex) {
  if (m_sidebarManager) {
    m_sidebarManager->applySidebarStateForCollection(collectionIndex);
  }
  if (m_settingsManager && m_itemScrollArea && m_collections) {
    SettingsUtils::applyHorizontalScrollbarSetting(
        m_itemScrollArea, collectionIndex, *m_collections);
    SettingsUtils::applyVerticalScrollbarSetting(
        m_itemScrollArea, collectionIndex, *m_collections);
  }
}

