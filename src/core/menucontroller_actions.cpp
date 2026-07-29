// Sibling TU of menucontroller.cpp: programmatic menu-action setup.
//
// Holds the setupActionX() functions that build the menu actions which are
// not declared in the .ui file: the File-menu Import/Export entries (Kart
// packages, theme presets), the Tools entries (Layout Profiles, Collection
// Health, Duplicates/Variants, Bulk Edit, Review Missing Metadata, Artwork
// Wizard), the New Library / Presentation Profiles wizards, the Help-menu
// Binding Visualizer, and Open Random Item. Each allocates its QAction
// (parented to `this`), parks it on the owning menu, and forwards
// triggered → the matching MenuControllerContext callback.
//
// Extracted from menucontroller.cpp to keep that file focused on the
// static .ui-backed action wiring and the setup-then-sync action groups.

#include "menucontroller.h"

#include "idatabasemanager.h"
#include "scrollmanager.h"
#include "ui_mainwindow.h"

#include <QAction>
#include <QMenu>
#include <QRandomGenerator>

// pick one media file at random from the active view and launch
// it. Uses ScrollManager::filePathForVisualIndex() — the same path Enter /
// double-click resolve through — so the result is the absolute, resolved
// path that DatabaseManager::getCollectionIndexForFile() can key off. The
// raw ScrollDataStore::filePaths() list won't work here: those are the
// relative entries from the items table, while the file→collection map is
// indexed by the resolved absolute paths.
void MenuController::setupActionOpenRandomItem() {
  if (!m_ctx.ui || !m_ctx.mainWindow || !m_ctx.ui->actionOpenRandomItem) return;

  m_ctx.ui->actionOpenRandomItem->setShortcutContext(Qt::ApplicationShortcut);
  m_ctx.mainWindow->addAction(m_ctx.ui->actionOpenRandomItem);

  connect(m_ctx.ui->actionOpenRandomItem, &QAction::triggered, this, [this]() {
    ScrollManager *scroll = m_ctx.getScrollManager ? m_ctx.getScrollManager() : nullptr;
    if (!scroll) return;

    const int total = scroll->getTotalItems();
    if (total <= 0) return;

    const int viewingIndex =
        m_ctx.getCurrentCollectionIndex ? m_ctx.getCurrentCollectionIndex() : -1;
    if (viewingIndex < 0) return;

    // Visual indices interleave subcollections + virtual folders + media
    // when unified sort is on, so a single random draw can land on a non-
    // launchable entry. Retry up to a sensible bound; if every draw misses
    // (collection has no media at all), bail silently.
    QString path;
    for (int attempt = 0; attempt < 32 && path.isEmpty(); ++attempt) {
      const int pick = QRandomGenerator::global()->bounded(total);
      path = scroll->filePathForVisualIndex(pick);
    }
    if (path.isEmpty()) return;

    // Resolve the file's *source* collection — same dance Enter /
    // double-click / context-menu launches do. Without this, a random pick
    // from a synthetic collection (playlist, aggregator) hands its
    // empty-launcher CollectionConfig to LaunchManager and trips "No
    // launcher configured".
    IDatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
    const int sourceIndex = db ? db->getCollectionIndexForFile(path) : -1;
    const int ownerIndex = (sourceIndex >= 0) ? sourceIndex : viewingIndex;

    if (m_ctx.onLaunchItem) {
      m_ctx.onLaunchItem(path, ownerIndex);
    }
  });
}

// File → Import entry for importing a.kart package.
void MenuController::setupActionImportKart() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_importKartAction = new QAction(tr("Import Kart..."), this);
  m_ctx.mainWindow->addAction(m_importKartAction);
  if (m_ctx.ui->menuImport) {
    m_ctx.ui->menuImport->addAction(m_importKartAction);
  }
  connect(m_importKartAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onImportKart) m_ctx.onImportKart();
  });
}

// File → Export entry for exporting the active collection as a.kart.
void MenuController::setupActionExportKart() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_exportKartAction = new QAction(tr("Export Collection as Kart..."), this);
  m_ctx.mainWindow->addAction(m_exportKartAction);
  if (m_ctx.ui->menuExport) {
    m_ctx.ui->menuExport->addAction(m_exportKartAction);
  }
  connect(m_exportKartAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onExportKart) m_ctx.onExportKart();
  });
}

// File → Import entry for a shareable theme preset (.kartend-theme.json).
// Distinct from Kart import — themes carry only the visual settings, not the
// collection's media paths / launcher / scraper config, so they can be shared
// across collections with completely different content.
void MenuController::setupActionImportTheme() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_importThemeAction = new QAction(tr("Import Theme..."), this);
  m_ctx.mainWindow->addAction(m_importThemeAction);
  if (m_ctx.ui->menuImport) {
    m_ctx.ui->menuImport->addAction(m_importThemeAction);
  }
  connect(m_importThemeAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onImportTheme) m_ctx.onImportTheme();
  });
}

void MenuController::setupActionExportTheme() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_exportThemeAction = new QAction(tr("Export Current Theme..."), this);
  m_ctx.mainWindow->addAction(m_exportThemeAction);
  if (m_ctx.ui->menuExport) {
    m_ctx.ui->menuExport->addAction(m_exportThemeAction);
  }
  connect(m_exportThemeAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onExportTheme) m_ctx.onExportTheme();
  });
}

// View-menu entry: named layout snapshots live with the layout controls they
// capture, below a separator from the live Layout / orientation toggles.
void MenuController::setupActionLayoutProfiles() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_layoutProfilesAction = new QAction(tr("Layout Profiles..."), this);
  m_ctx.mainWindow->addAction(m_layoutProfilesAction);
  if (m_ctx.ui->menuView) {
    m_ctx.ui->menuView->addSeparator();
    m_ctx.ui->menuView->addAction(m_layoutProfilesAction);
  }
  connect(m_layoutProfilesAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onManageLayoutProfiles) m_ctx.onManageLayoutProfiles();
  });
}

void MenuController::setupActionCollectionHealth() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_collectionHealthAction = new QAction(tr("Collection Health..."), this);
  m_ctx.mainWindow->addAction(m_collectionHealthAction);
  if (m_ctx.ui->menuTools) {
    m_ctx.ui->menuTools->addAction(m_collectionHealthAction);
  }
  connect(m_collectionHealthAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onShowCollectionHealth) m_ctx.onShowCollectionHealth();
  });
}

void MenuController::setupActionVariantGrouping() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_variantGroupingAction = new QAction(tr("Duplicates and variants..."), this);
  m_ctx.mainWindow->addAction(m_variantGroupingAction);
  if (m_ctx.ui->menuTools) {
    m_ctx.ui->menuTools->addAction(m_variantGroupingAction);
  }
  connect(m_variantGroupingAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onShowVariantGrouping) m_ctx.onShowVariantGrouping();
  });
}

void MenuController::setupActionBulkEdit() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_bulkEditAction = new QAction(tr("Bulk Edit Items..."), this);
  m_ctx.mainWindow->addAction(m_bulkEditAction);
  if (m_ctx.ui->menuTools) {
    m_ctx.ui->menuTools->addAction(m_bulkEditAction);
  }
  connect(m_bulkEditAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onBulkEdit) m_ctx.onBulkEdit();
  });
}

void MenuController::setupActionReviewMissingMetadata() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_reviewMissingMetadataAction = new QAction(tr("Review Missing Metadata..."), this);
  m_ctx.mainWindow->addAction(m_reviewMissingMetadataAction);
  if (m_ctx.ui->menuTools) {
    m_ctx.ui->menuTools->addAction(m_reviewMissingMetadataAction);
  }
  connect(m_reviewMissingMetadataAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onReviewMissingMetadata) m_ctx.onReviewMissingMetadata();
  });
}

void MenuController::setupActionArtworkWizard() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_artworkWizardAction = new QAction(tr("Assign Missing Artwork..."), this);
  m_ctx.mainWindow->addAction(m_artworkWizardAction);
  if (m_ctx.ui->menuTools) {
    m_ctx.ui->menuTools->addAction(m_artworkWizardAction);
  }
  connect(m_artworkWizardAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onArtworkWizard) m_ctx.onArtworkWizard();
  });
}

void MenuController::setupActionBindingVisualizer() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  // Sits in the Help menu next to Usage Statistics / Shortcuts since
  // it's a read-only diagnostic surface for the user's input config.
  m_bindingVisualizerAction = new QAction(tr("Binding Visualizer..."), this);
  m_ctx.mainWindow->addAction(m_bindingVisualizerAction);
  if (m_ctx.ui->menuHelp) {
    m_ctx.ui->menuHelp->addAction(m_bindingVisualizerAction);
  }
  connect(m_bindingVisualizerAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onShowBindings) m_ctx.onShowBindings();
  });
}

// Heads the File menu — "create a new thing" is the conventional first entry.
// Anchored ahead of Soft Refresh rather than appended, since appending would
// land it below Exit.
void MenuController::setupActionNewLibraryWizard() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_newLibraryWizardAction = new QAction(tr("New Library Wizard..."), this);
  m_ctx.mainWindow->addAction(m_newLibraryWizardAction);
  if (m_ctx.ui->menuFile) {
    QAction *const anchor = m_ctx.ui->actionSoftRefresh;
    if (anchor) {
      m_ctx.ui->menuFile->insertAction(anchor, m_newLibraryWizardAction);
      m_ctx.ui->menuFile->insertSeparator(anchor);
    } else {
      m_ctx.ui->menuFile->addAction(m_newLibraryWizardAction);
    }
  }
  connect(m_newLibraryWizardAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onNewLibraryWizard) m_ctx.onNewLibraryWizard();
  });
}

void MenuController::setupActionPresentationProfiles() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_presentationProfilesAction = new QAction(tr("Presentation Profiles..."), this);
  m_ctx.mainWindow->addAction(m_presentationProfilesAction);
  if (m_ctx.ui->menuView) {
    m_ctx.ui->menuView->addAction(m_presentationProfilesAction);
  }
  connect(m_presentationProfilesAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onPresentationProfiles) m_ctx.onPresentationProfiles();
  });
}
