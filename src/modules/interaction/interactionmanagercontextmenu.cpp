// Sibling translation unit for InteractionManager.
// Right-click context menu implementation (Kartend-g6f).
// These remain InteractionManager members; this is a translation-unit split.
#include "interactionmanager.h"

#include <QApplication>
#include <QMenu>

#include "collectionutils.h"
#include "databasemanager.h"
#include "itemwidget.h"
#include "launchmanager.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "sidebarmanager.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcInteractionManager)

void InteractionManager::showContextMenu(ItemWidget *widget, int visualIndex,
                                         const QPoint &globalPos) {
  if (!widget || visualIndex < 0) {
    return;
  }

  // Select the right-clicked item so the user sees which item is targeted
  selectItemByIndex(visualIndex, true);

  const bool isSubcollection = widget->isSubcollection();
  const bool isVirtualFolder = widget->isVirtualFolder();
  const bool isMediaItem = !isSubcollection && !isVirtualFolder;
  const QString filePath = widget->getFilePath();

  QMenu menu;

  // --- Launch action (only for media items with a file path) ---
  if (isMediaItem && !filePath.isEmpty()) {
    QAction *launchAction = menu.addAction(tr("Launch"));
    QObject::connect(launchAction, &QAction::triggered, this, [this, filePath]() {
      if (!m_databaseManager || !m_currentCollectionIndex) {
        return;
      }
      const int cIdx = m_databaseManager->getCollectionIndexForFile(filePath);
      const int ownerIdx = (cIdx >= 0 ? cIdx : *m_currentCollectionIndex);
      launchItemWithCollection(filePath, ownerIdx);
    });
  }

  // --- Open (enter) action for subcollections and virtual folders ---
  if (isSubcollection || isVirtualFolder) {
    QAction *openAction = menu.addAction(tr("Open"));
    QObject::connect(openAction, &QAction::triggered, this, [this]() {
      if (m_scrollManager) {
        const int totalItems = m_scrollManager->getTotalItems();
        processEnterOrReturnKey(totalItems);
      }
    });
  }

  menu.addSeparator();

  // --- Toggle sidebar (properties) action ---
  QAction *propertiesAction = menu.addAction(tr("Properties"));
  QObject::connect(propertiesAction, &QAction::triggered, this, [this]() {
    if (m_sidebarManager) {
      m_sidebarManager->toggleSidebar();
    }
  });

  // --- Refresh action (soft reload of current collection) ---
  QAction *refreshAction = menu.addAction(tr("Refresh"));
  QObject::connect(refreshAction, &QAction::triggered, this, [this]() {
    if (m_navigationManager && m_currentCollectionIndex && *m_currentCollectionIndex >= 0) {
      m_navigationManager->safeReloadCollection(*m_currentCollectionIndex);
    }
  });

  menu.exec(globalPos);
}
