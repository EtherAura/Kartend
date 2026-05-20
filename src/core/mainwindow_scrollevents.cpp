// MainWindow's reactions to ScrollManager view-mode / column-resize /
// CoverFlow activation signals.
//
// Extracted from mainwindowwiring.cpp during the responsibility-based TU
// split. These slot handlers all share one concern: react to changes
// originating in ScrollManager (sort-mode flip, list-column resize,
// CoverFlow activation/item-activate, artwork preview visibility) by
// updating MainWindow's view state, persisting the affected
// GeneralSettings fields, and forwarding the side effect through
// InteractionManager / NavigationManager / DetailsPaneManager / SettingsManager.

#include <QString>

#include "collectionutils.h"
#include "databasemanager.h"
#include "detailspanemanager.h"
#include "interactionmanager.h"
#include "isettingsmanager.h"
#include "mainwindow.h"
#include "navigationmanager.h"
#include "scrollmanager.h"

void MainWindow::onSortModeChangeRequested(SortMode sortMode) {
  // List view header column click triggers sort mode change.
  if (!getNavigationManager()) {
    return;
  }
  // Capture currently selected file path to restore after reload.
  if (getInteractionManager() && getScrollManager()) {
    QString selectedPath = getInteractionManager()->selectedFilePath();
    if (!selectedPath.isEmpty()) {
      getScrollManager()->setPendingSelectionRestoreByPath(selectedPath);
    }
  }
  m_generalSettings.sortMode = sortMode;
  if (getSettingsManager()) {
    getSettingsManager()->saveGeneralSettings(m_generalSettings);
  }
  getNavigationManager()->safeReloadCollection(currentCollectionIndex);
}

void MainWindow::onSelectItemByIndex(int index) {
  // Restore selection by index after sort change finds the item.
  if (!getInteractionManager()) {
    return;
  }
  getInteractionManager()->selectItemByIndex(index, true);
  // Instantly scroll to make the item visible (no animation).
  getInteractionManager()->applyImmediateViewportPositioningForSelection(index);
}

void MainWindow::onCoverFlowActiveChanged(bool active) {
  // Yield sidebar viewport space when entering CoverFlow, restore the
  // persisted per-collection state when leaving.
  if (auto *dpm = getDetailsPaneManager()) {
    dpm->setExternallyHidden(active);
  }
}

void MainWindow::onArtworkPreviewVisibilityChanged(bool visible) {
  // Bug #7: lower the sidebar while the artwork preview overlay is showing
  // so the overlay (parented to the top-level window) stays on top. Restored
  // on hide.
  if (auto *dpm = getDetailsPaneManager()) {
    dpm->setOverlayActive(visible);
  }
}

void MainWindow::onCoverFlowItemActivated(int index) {
  // Cover-flow activates a card → land selection on it then route through
  // the existing launch path. Subcollection / virtual-folder activations are
  // handled by ScrollManager itself via subcollectionEntered /
  // virtualFolderEntered above so they don't reach this slot. The owning
  // collection comes from DatabaseManager — the launcher / core / params are
  // configured per-collection and using currentCollectionIndex would mis-launch
  // any item inherited from a subcollection in showAllSubcollectionItems mode
  // (surfaces as "no launcher configured").
  if (!getInteractionManager() || !getScrollManager()) {
    return;
  }
  getInteractionManager()->selectItemByIndex(index, true);
  const QString filePath = getScrollManager()->filePathForVisualIndex(index);
  if (filePath.isEmpty()) {
    return;
  }
  int ownerIdx = currentCollectionIndex;
  if (getDatabaseManager()) {
    const int detected = getDatabaseManager()->getCollectionIndexForFile(filePath);
    if (detected >= 0) {
      ownerIdx = detected;
    }
  }
  getInteractionManager()->launchItemWithCollection(filePath, ownerIdx);
}

void MainWindow::onListColumnWidthChanged(int width) {
  // Persist list column width when user resizes.
  m_generalSettings.listCollectionColumnWidth = width;
  if (getSettingsManager()) {
    getSettingsManager()->saveGeneralSettings(m_generalSettings);
  }
}

void MainWindow::onListArtworkColumnWidthChanged(int width) {
  // Persist list artwork column width when user resizes.
  m_generalSettings.listArtworkColumnWidth = width;
  if (getSettingsManager()) {
    getSettingsManager()->saveGeneralSettings(m_generalSettings);
  }
}
