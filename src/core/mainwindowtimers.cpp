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
#include "detailspane.h"
#include "detailspanemanager.h"
#include "interactionmanager.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcMainWindow)

void MainWindow::setupInitialTimers() {
  // Defer initial horizontal centering until after the first show event so
  // the scroll area has accurate viewport metrics to center against.
  QTimer::singleShot(UIConstants::DetailsPane::INITIAL_CENTER_SCROLL_DELAY_MS, this, [this]() {
    if (getScrollManager()) {
      getScrollManager()->centerHorizontalScrollbar(currentCollectionIndex, m_collections);
    }
  });

  // First-run wizard takes precedence over the legacy "create your first
  // collection" QInputDialog. Once the user has either completed or
  // explicitly skipped the wizard, firstRunComplete stays true forever and
  // the legacy empty-collections prompt remains as the backstop for power
  // users who later delete every collection.
  if (!m_generalSettings.firstRunComplete) {
    QTimer::singleShot(0, this, [this]() {
      showFirstRunWizard();
      // After the wizard the user may still have an empty library (Skip
      // path). Fall through to the legacy prompt so they're not stranded
      // on a blank window with no obvious next step.
      if (m_collections.isEmpty()) {
        setupInitialTimersEmptyCollections();
      } else {
        setupInitialTimersWithCollections();
      }
    });
    return;
  }

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
      context.detailsPaneManager = getDetailsPaneManager();
      context.scrollManager = getScrollManager();
      context.navigationManager = getNavigationManager();
      context.databaseManager = getDatabaseManager();
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
    // Synthetic home view takes priority when explicitly requested AND the
    // user hasn't pinned a specific startup collection. A specific collection
    // wins so a CLI override (--collection) or per-launch override still works.
    const QString startupName = m_generalSettings.startupCollection.trimmed();
    if (m_generalSettings.useHomeView && startupName.isEmpty()) {
      if (getNavigationManager()) {
        m_suppressStartupScanOverlays = true;
        getNavigationManager()->loadRootView();
      }
      return;
    }

    int rootIndex = -1;
    // Honor the configured startup collection, if set and still present.
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
