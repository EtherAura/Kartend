#include "scrolleventscontroller.h"

#include <QString>

#include "collectiontypes.h"
#include "databasemanager.h"
#include "detailspanemanager.h"
#include "errorpresentation.h"
#include "interactionmanager.h"
#include "isettingsmanager.h"
#include "navigationmanager.h"
#include "scrollmanager.h"

ScrollEventsController::ScrollEventsController(QObject *parent) : QObject(parent) {}

ScrollEventsController::~ScrollEventsController() = default;

void ScrollEventsController::setContext(const ScrollEventsControllerContext &context) {
  m_ctx = context;
}

void ScrollEventsController::onSortModeChangeRequested(SortMode sortMode) {
  // List view header column click triggers sort mode change.
  auto *navigation = m_ctx.getNavigationManager ? m_ctx.getNavigationManager() : nullptr;
  if (!navigation) {
    return;
  }
  auto *interaction = m_ctx.getInteractionManager ? m_ctx.getInteractionManager() : nullptr;
  auto *scroll = m_ctx.getScrollManager ? m_ctx.getScrollManager() : nullptr;
  // Capture currently selected file path to restore after reload.
  if (interaction && scroll) {
    QString selectedPath = interaction->selectedFilePath();
    if (!selectedPath.isEmpty()) {
      scroll->setPendingSelectionRestoreByPath(selectedPath);
    }
  }
  if (auto *settings = m_ctx.getGeneralSettings ? m_ctx.getGeneralSettings() : nullptr) {
    settings->view.sortMode = sortMode;
    if (auto *sm = m_ctx.getSettingsManager ? m_ctx.getSettingsManager() : nullptr) {
      ErrorPresentation::reportSaveResult(sm->saveGeneralSettings(*settings), "general settings",
                                          false);
    }
  }
  const int currentIndex = m_ctx.getCurrentCollectionIndex ? m_ctx.getCurrentCollectionIndex() : -1;
  navigation->safeReloadCollection(currentIndex);
}

void ScrollEventsController::onSelectItemByIndex(int index) {
  auto *interaction = m_ctx.getInteractionManager ? m_ctx.getInteractionManager() : nullptr;
  if (!interaction) {
    return;
  }
  // Kartend-ic4h6: cancel any pending automatic selection restore BEFORE
  // applying this selection, exactly as the grid click / arrow-key /
  // alphabetic-jump paths already do. This slot is the missing member of
  // that set: it serves cover flow's user-driven selection (wheel, card
  // click, arrows — every emit site of CoverFlowWidget's
  // selectionChangeRequested is a user gesture). Without the cancel,
  // SelectionRestoreCoordinator's staggered verification timers read
  // "current != restore target" as "the restore has not landed" and
  // re-assert the restored index — snapping the selection back to the
  // first item seconds after the user moved (runtime-confirmed: repeated
  // beginSelectionRestore targetIndex=0 in the reporter's trace).
  //
  // The signal's only other emitter is ScrollManager's restore-by-path
  // (sort-change reload). Cancelling for it is also correct: that restore
  // is the more precise, path-based expression of the same user intent,
  // and the coordinator's coarser index-based verify must not clobber it
  // either. The coordinator's own beginSelectionRestore does NOT route
  // through this slot, so a legitimate session restore cannot cancel
  // itself here.
  interaction->cancelPendingSelectionRestore();
  interaction->selectItemByIndex(index, true);
  // Instantly scroll to make the item visible (no animation).
  interaction->applyImmediateViewportPositioningForSelection(index);
}

void ScrollEventsController::onCoverFlowActiveChanged(bool active) {
  // Yield sidebar viewport space when entering CoverFlow, restore the
  // persisted per-collection state when leaving.
  if (auto *dpm = m_ctx.getDetailsPaneManager ? m_ctx.getDetailsPaneManager() : nullptr) {
    dpm->setExternallyHidden(active);
  }
}

void ScrollEventsController::onArtworkPreviewVisibilityChanged(bool visible) {
  // Bug #7: lower the sidebar while the artwork preview overlay is showing
  // so the overlay (parented to the top-level window) stays on top. Restored
  // on hide.
  if (auto *dpm = m_ctx.getDetailsPaneManager ? m_ctx.getDetailsPaneManager() : nullptr) {
    dpm->setOverlayActive(visible);
  }
}

void ScrollEventsController::onCoverFlowItemActivated(int index) {
  // Cover-flow activates a card → land selection on it then route through
  // the existing launch path. Subcollection / virtual-folder activations are
  // handled by ScrollManager itself via subcollectionEntered /
  // virtualFolderEntered above so they don't reach this slot. The owning
  // collection comes from DatabaseManager — the launcher / core / params are
  // configured per-collection and using currentCollectionIndex would mis-launch
  // any item inherited from a subcollection in showAllSubcollectionItems mode
  // (surfaces as "no launcher configured").
  auto *interaction = m_ctx.getInteractionManager ? m_ctx.getInteractionManager() : nullptr;
  auto *scroll = m_ctx.getScrollManager ? m_ctx.getScrollManager() : nullptr;
  if (!interaction || !scroll) {
    return;
  }
  interaction->selectItemByIndex(index, true);
  const QString filePath = scroll->filePathForVisualIndex(index);
  if (filePath.isEmpty()) {
    return;
  }
  int ownerIdx = m_ctx.getCurrentCollectionIndex ? m_ctx.getCurrentCollectionIndex() : -1;
  if (auto *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr) {
    const int detected = db->getCollectionIndexForFile(filePath);
    if (detected >= 0) {
      ownerIdx = detected;
    }
  }
  interaction->launchItemWithCollection(filePath, ownerIdx);
}

void ScrollEventsController::onListColumnWidthChanged(int width) {
  // Persist list column width when user resizes. The header emits this once
  // per mouse-move tick during a drag, so the write-through to the live
  // struct is immediate (the resize applies visually elsewhere) while the
  // full-INI persist goes through the host's debounced saver when wired.
  auto *settings = m_ctx.getGeneralSettings ? m_ctx.getGeneralSettings() : nullptr;
  if (!settings) {
    return;
  }
  settings->view.listCollectionColumnWidth = width;
  if (m_ctx.scheduleSettingsSave) {
    m_ctx.scheduleSettingsSave();
    return;
  }
  if (auto *sm = m_ctx.getSettingsManager ? m_ctx.getSettingsManager() : nullptr) {
    ErrorPresentation::reportSaveResult(sm->saveGeneralSettings(*settings), "general settings",
                                        false);
  }
}

void ScrollEventsController::onListArtworkColumnWidthChanged(int width) {
  // Persist list artwork column width when user resizes. Same per-tick
  // emission + debounced-persist shape as onListColumnWidthChanged above.
  auto *settings = m_ctx.getGeneralSettings ? m_ctx.getGeneralSettings() : nullptr;
  if (!settings) {
    return;
  }
  settings->view.listArtworkColumnWidth = width;
  if (m_ctx.scheduleSettingsSave) {
    m_ctx.scheduleSettingsSave();
    return;
  }
  if (auto *sm = m_ctx.getSettingsManager ? m_ctx.getSettingsManager() : nullptr) {
    ErrorPresentation::reportSaveResult(sm->saveGeneralSettings(*settings), "general settings",
                                        false);
  }
}
