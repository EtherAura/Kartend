// Sibling TU: initial timer setup methods for MainWindow.
#include <QApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QPixmapCache>
#include <QTimer>

#include "applicationmanager.h"
#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "interactionmanager.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "metadatasidebar.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "sidebarmanager.h"
#include "timerutils.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcMainWindow)

void MainWindow::setupInitialTimers() {
  // Defer initial horizontal centering until after the first show event so
  // the scroll area has accurate viewport metrics to center against.
  QTimer::singleShot(UIConstants::Sidebar::INITIAL_CENTER_SCROLL_DELAY_MS, this, [this]() {
    if (getScrollManager()) {
      getScrollManager()->centerHorizontalScrollbar(currentCollectionIndex, m_collections);
    }
  });

  if (m_collections.isEmpty()) {
    setupInitialTimersEmptyCollections();
  } else {
    setupInitialTimersWithCollections();
  }
}

void MainWindow::setupInitialTimersEmptyCollections() {
  // Defer collection creation until after the main window is fully shown -
  // ensures proper parent-child relationship and window stacking order
  QTimer::singleShot(0, this, [this]() {
    // Prompt user to create their first collection
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Create First Collection"),
                                         tr("Enter a name for your first collection:"),
                                         QLineEdit::Normal, "", &ok);

    if (!ok || name.trimmed().isEmpty()) {
      // User cancelled - show message and close
      QMessageBox::information(this, tr("No Collection Created"),
                               tr("Kartend requires at least one collection to function. "
                                  "Please restart the application to try again."));
      return;
    }

    // Create the first collection with the given name
    CollectionConfig newCollection;
    newCollection.name = name.trimmed();
    newCollection.gridWidth = UIConstants::Grid::DEFAULT_WIDTH;
    newCollection.parentCollectionIndex = -1;
    newCollection.isSubcollection = false;
    m_collections.append(newCollection);

    // Save the new collection
    if (getSettingsManager()) {
      getSettingsManager()->saveCollections(m_collections);
    }

    // Rebuild hierarchy cache with the new collection
    rebuildHierarchyCache();

    // Now open settings dialog for the user to configure the collection
    if (getSettingsManager()) {
      currentCollectionIndex = 0;
      SettingsDialogContext context;
      context.parent = this;
      context.collections = &m_collections;
      context.currentCollectionIndex = &currentCollectionIndex;
      context.sidebarManager = getSidebarManager();
      context.scrollManager = getScrollManager();
      context.navigationManager = getNavigationManager();
      getSettingsManager()->openSettingsDialog(context);

      if (!m_collections.isEmpty()) {
        currentCollectionIndex = 0;
        if (getNavigationManager()) {
          getNavigationManager()->showCollectionItems(0);
        }
      }
    }
  });
}

void MainWindow::setupInitialTimersWithCollections() {
  // Defer collection loading until after the main window is fully shown -
  // allows Qt to complete layout calculations before populating the grid
  QTimer::singleShot(0, this, [this]() {
    int rootIndex = -1;
    // Honor the configured startup collection, if set and still present.
    const QString startupName = m_generalSettings.startupCollection.trimmed();
    if (!startupName.isEmpty()) {
      for (int i = 0; i < m_collections.size(); ++i) {
        if (m_collections[i].name == startupName) {
          rootIndex = i;
          break;
        }
      }
    }
    if (rootIndex < 0) {
      for (int i = 0; i < m_collections.size(); ++i) {
        if (m_collections[i].parentCollectionIndex == -1) {
          rootIndex = i;
          break;
        }
      }
    }
    if (rootIndex < 0 && !m_collections.isEmpty()) {
      rootIndex = 0;
    }
    if (rootIndex >= 0) {
      if (getNavigationManager()) {
        // The initial fetchItemCount path may trigger a rescan; keep the UI
        // navigable by suppressing any blocking overlay for this startup scan.
        m_suppressStartupScanOverlays = true;
        getNavigationManager()->showCollectionItems(rootIndex);
      }
    }
  });
}
