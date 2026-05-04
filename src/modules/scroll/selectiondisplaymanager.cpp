// SelectionDisplayManager: owns selection overlay, state tracker, list header,
// and artwork preview overlay. Extracted from ScrollManager (Kartend-3u5).
// Selection update logic moved here from scrollmanagerselection.cpp
// (Kartend-p79).
#include "selectiondisplaymanager.h"

#include "arrowkeyscrollhelper.h"
#include "artworkpreviewoverlay.h"
#include "gridutils.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "itemwidgetfactory.h"
#include "listheaderwidget.h"
#include "scrollhelpers.h"
#include "selectioncoordinator.h"
#include "selectionoverlaymanager.h"
#include "selectionstatetracker.h"
#include "settingsutils.h"
#include "uiconstants.h"

#include <algorithm>
#include <QHash>
#include <QLoggingCategory>
#include <QScrollArea>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace {
Q_LOGGING_CATEGORY(lcSelectionDisplay, "kartend.selectiondisplay")
}

#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcSelectionDisplay().isDebugEnabled()) {                                                   \
      qCDebug(lcSelectionDisplay) << msg;                                                          \
    }                                                                                              \
  } while (0)

SelectionDisplayManager::SelectionDisplayManager(QObject *parent)
    : QObject(parent), m_overlay(std::make_unique<SelectionOverlayManager>(this)),
      m_stateTracker(std::make_unique<SelectionStateTracker>()) {}

SelectionDisplayManager::~SelectionDisplayManager() {
  destroyListHeader();
}

void SelectionDisplayManager::applyGeneralSettings(const GeneralSettings *settings) {
  if (!settings) {
    return;
  }
  if (settings->listCollectionColumnWidth > 0) {
    m_collectionColumnWidth = settings->listCollectionColumnWidth;
  }
  if (settings->listArtworkColumnWidth > 0) {
    m_artworkColumnWidth = settings->listArtworkColumnWidth;
  }
}

void SelectionDisplayManager::destroyListHeader() {
  if (m_listHeader) {
    m_listHeader->deleteLater();
    m_listHeader = nullptr;
  }
}

// ─────────────────────────────────────────────────────────────────────────
// List header
// ─────────────────────────────────────────────────────────────────────────

// Header is parented to viewport (not virtual container) so it stays fixed
// while scrolling.
void SelectionDisplayManager::updateListHeader() {
  if (!m_context || !m_metrics) {
    return;
  }
  const bool isListMode = (m_context->config.viewType == ViewType::List);

  if (isListMode && m_mediaScrollArea && m_mediaScrollArea->viewport()) {
    QWidget *viewport = m_mediaScrollArea->viewport();
    if (!m_listHeader) {
      // Parent to viewport so header stays fixed while content scrolls
      m_listHeader = new ListHeaderWidget(viewport);
      // Connect column click signal to sort handler
      connect(m_listHeader, &ListHeaderWidget::columnClicked, this,
              &SelectionDisplayManager::onListColumnClicked);
      // Connect column width change signals for drag-to-resize
      connect(m_listHeader, &ListHeaderWidget::columnWidthChanged, this,
              &SelectionDisplayManager::onListColumnWidthChanged);
      connect(m_listHeader, &ListHeaderWidget::artworkColumnWidthChanged, this,
              &SelectionDisplayManager::onListArtworkColumnWidthChanged);
    }
    // Always sync column widths - settings may have been loaded after header
    // creation
    m_listHeader->setCollectionColumnWidth(m_collectionColumnWidth);
    m_listHeader->setArtworkColumnWidth(m_artworkColumnWidth);

    // Position header at top of viewport - calculate x position based on
    // container position
    int headerWidth = m_metrics->itemWidth + (m_metrics->margins * 2);
    int containerX = m_virtualContainer ? m_virtualContainer->x() : 0;
    m_listHeader->setGeometry(containerX, 0, headerWidth, UIConstants::ListView::HEADER_HEIGHT);
    m_listHeader->show();
    m_listHeader->raise(); // Keep above scrolling content
    m_listHeader->setAttribute(Qt::WA_TransparentForMouseEvents,
                               false); // Ensure header receives clicks
    qCDebug(lcSelectionDisplay) << "updateListHeader: header geometry=" << m_listHeader->geometry()
                                << "raised, accepts mouse events";
  } else if (m_listHeader) {
    // Hide header in grid mode
    m_listHeader->hide();
  }
}

void SelectionDisplayManager::onListColumnClicked(ListSortColumn column) {
  if (!m_listHeader) {
    return;
  }

  // Toggle direction if clicking the same column
  static ListSortColumn lastColumn = ListSortColumn::Name;
  static bool ascending = true;

  if (column == lastColumn) {
    ascending = !ascending;
  } else {
    lastColumn = column;
    ascending = true;
  }

  m_listHeader->setSortColumn(column, ascending);

  // Determine sort mode based on column and direction and emit signal
  if (column == ListSortColumn::Name) {
    SortMode newSortMode = ascending ? SortMode::NameAscending : SortMode::NameDescending;
    qCDebug(lcSelectionDisplay) << "onListColumnClicked: Name column, ascending=" << ascending
                                << "sortMode=" << static_cast<int>(newSortMode);
    emit sortModeChangeRequested(newSortMode);
  } else if (column == ListSortColumn::Collection) {
    SortMode newSortMode =
        ascending ? SortMode::CollectionAscending : SortMode::CollectionDescending;
    qCDebug(lcSelectionDisplay) << "onListColumnClicked: Collection column, ascending=" << ascending
                                << "sortMode=" << static_cast<int>(newSortMode);
    emit sortModeChangeRequested(newSortMode);
  } else if (column == ListSortColumn::Artwork) {
    SortMode newSortMode = ascending ? SortMode::ArtworkFirst : SortMode::ArtworkLast;
    qCDebug(lcSelectionDisplay) << "onListColumnClicked: Artwork column, ascending=" << ascending
                                << "sortMode=" << static_cast<int>(newSortMode);
    emit sortModeChangeRequested(newSortMode);
  }
}

void SelectionDisplayManager::onListColumnWidthChanged(int collectionWidth) {
  m_collectionColumnWidth = collectionWidth;

  // Update factory so new widgets get the correct width
  if (m_widgetFactory) {
    m_widgetFactory->setCollectionColumnWidth(collectionWidth);
  }

  // Update all visible widgets with the new column width
  if (m_activeWidgets) {
    for (auto it = m_activeWidgets->begin(); it != m_activeWidgets->end(); ++it) {
      ItemWidget *widget = it.value();
      if (widget && widget->isListMode()) {
        widget->setCollectionColumnWidth(collectionWidth);
      }
    }
  }

  // Emit signal for persistence
  emit listColumnWidthChanged(collectionWidth);
}

void SelectionDisplayManager::onListArtworkColumnWidthChanged(int artworkWidth) {
  m_artworkColumnWidth = artworkWidth;

  // Update factory so new widgets get the correct width
  if (m_widgetFactory) {
    m_widgetFactory->setArtworkColumnWidth(artworkWidth);
  }

  // Update all visible widgets with the new column width
  if (m_activeWidgets) {
    for (auto it = m_activeWidgets->begin(); it != m_activeWidgets->end(); ++it) {
      ItemWidget *widget = it.value();
      if (widget && widget->isListMode()) {
        widget->setArtworkColumnWidth(artworkWidth);
      }
    }
  }

  // Emit signal for persistence
  emit listArtworkColumnWidthChanged(artworkWidth);
}

// ─────────────────────────────────────────────────────────────────────────
// Artwork preview overlay
// ─────────────────────────────────────────────────────────────────────────

bool SelectionDisplayManager::isArtworkPreviewVisible() const {
  return m_artworkPreviewOverlay && m_artworkPreviewOverlay->isVisible();
}

bool SelectionDisplayManager::hideArtworkPreview() {
  if (isArtworkPreviewVisible()) {
    m_artworkPreviewOverlay->hideOverlay();
    return true;
  }
  return false;
}

void SelectionDisplayManager::showArtworkPreview(const QString &filePath,
                                                 const QString &artworkDir) {
  if (!m_mediaScrollArea) {
    return;
  }

  // Create overlay lazily on first use
  if (!m_artworkPreviewOverlay) {
    m_artworkPreviewOverlay = std::make_unique<ArtworkPreviewOverlay>(m_mediaScrollArea);
    connect(m_artworkPreviewOverlay.get(), &ArtworkPreviewOverlay::launchRequested, this,
            &SelectionDisplayManager::artworkPreviewLaunchRequested);
    // Kartend-63e bug #7: forward overlay visibility so SidebarManager can
    // lower the sidebar while the overlay is on top.
    connect(m_artworkPreviewOverlay.get(), &ArtworkPreviewOverlay::visibilityChanged, this,
            &SelectionDisplayManager::artworkPreviewVisibilityChanged);
  }

  // Show artwork preview using the item's collection artwork directory
  m_artworkPreviewOverlay->showArtworkForFile(filePath, artworkDir);
}

void SelectionDisplayManager::showMediaPreview(const QString &filePath, const QString &artworkDir,
                                               const QString &videoDir) {
  if (!m_mediaScrollArea) {
    return;
  }
  if (!m_artworkPreviewOverlay) {
    m_artworkPreviewOverlay = std::make_unique<ArtworkPreviewOverlay>(m_mediaScrollArea);
    connect(m_artworkPreviewOverlay.get(), &ArtworkPreviewOverlay::launchRequested, this,
            &SelectionDisplayManager::artworkPreviewLaunchRequested);
    // Kartend-63e bug #7: forward overlay visibility so SidebarManager can
    // lower the sidebar while the overlay is on top.
    connect(m_artworkPreviewOverlay.get(), &ArtworkPreviewOverlay::visibilityChanged, this,
            &SelectionDisplayManager::artworkPreviewVisibilityChanged);
  }
  m_artworkPreviewOverlay->showMediaForFile(filePath, artworkDir, videoDir);
}

// ─────────────────────────────────────────────────────────────────────────
// Selection update logic (moved from ScrollManager, Kartend-p79)
// ─────────────────────────────────────────────────────────────────────────

void SelectionDisplayManager::onArrowKeyViewUpdate() {
  const int totalItems = m_totalItemsProvider ? m_totalItemsProvider() : 0;
  if (!m_arrowKeyScrollHelper || !m_virtualContainer || !m_stateTracker ||
      !m_stateTracker->hasSelection() || m_stateTracker->lastSelectedIndex() >= totalItems ||
      !m_metrics || !m_itemPosition) {
    return;
  }

  // Update metrics for the helper
  m_arrowKeyScrollHelper->setItemMetrics(m_metrics->itemHeight, m_metrics->verticalSpacing,
                                         m_metrics->margins);

  // Delegate to helper with position callback
  m_arrowKeyScrollHelper->performUpdate(m_stateTracker->lastSelectedIndex(), totalItems,
                                        m_metrics->itemsPerRow,
                                        [this](int idx) { return m_itemPosition(idx).y(); });
}

auto SelectionDisplayManager::selectionOverlayRectForIndex(int visualIndex) const -> QRect {
  const int totalItems = m_totalItemsProvider ? m_totalItemsProvider() : 0;

  // Delegate to coordinator if available (shares the same calculation)
  if (m_selectionCoordinator) {
    return m_selectionCoordinator->rectForIndex(visualIndex, totalItems);
  }

  // Fallback for when coordinator is not configured
  if (visualIndex < 0 || visualIndex >= totalItems || !m_metrics || !m_itemPosition) {
    return {};
  }
  QPoint pos = m_itemPosition(visualIndex);
  return SelectionOverlayManager::overlayRectForPosition(pos, m_metrics->itemWidth,
                                                         m_metrics->itemHeight);
}

void SelectionDisplayManager::refreshSelectionOverlayState() {
  if (!m_overlay || !m_stateTracker) {
    return;
  }

  debugLog(QString("refreshSelectionOverlayState: lastSelectedIndex=%1 forceVisible=%2")
               .arg(m_stateTracker->lastSelectedIndex())
               .arg(m_overlay->isForceVisible()));

  if (!m_overlay->shouldKeepVisible()) {
    debugLog("  HIDING overlay (not force visible)");
    m_overlay->hide();

    bool glideWasActive = m_state && m_state->glideAnimating();
    if (m_state) {
      m_state->setGlideAnimating(false);
    }

    if (glideWasActive && m_stateTracker->hasSelection() && m_ensureWidget && m_activeWidgets) {
      m_ensureWidget(m_stateTracker->lastSelectedIndex());
      if (auto *selectedWidget =
              m_activeWidgets->value(m_stateTracker->lastSelectedIndex(), nullptr)) {
        debugLog("  requesting widget repaint for selection border");
        selectedWidget->update();
      }
    }
    return;
  }

  // Don't interfere with an ongoing selection overlay animation
  if (m_overlay->isAnimating()) {
    debugLog("  animation running, just show/raise");
    m_overlay->raise();
    return;
  }

  // During click-hold, we respect the current visibility state (managed by move
  // handlers) If visible, keep it on top. If hidden (e.g. wrapped row), keep it
  // hidden.
  if (m_overlay->isForceVisible()) {
    if (m_overlay->isVisible()) {
      debugLog("  click-hold mode, raising visible overlay");
      m_overlay->raise();
    } else {
      debugLog("  click-hold mode, overlay hidden (respecting state)");
    }
    return;
  }

  if (!m_stateTracker->hasSelection()) {
    return;
  }

  // Position overlay directly (non-click-hold case or initial positioning)
  QRect rect = selectionOverlayRectForIndex(m_stateTracker->lastSelectedIndex());
  debugLog(QString("  setting geometry to rect: x=%1 y=%2 w=%3 h=%4")
               .arg(rect.x())
               .arg(rect.y())
               .arg(rect.width())
               .arg(rect.height()));
  if (rect.isValid()) {
    m_overlay->showAtRect(rect);
  }
}

void SelectionDisplayManager::setForceSelectionOverlayVisible(bool force) {
  debugLog(QString("setForceSelectionOverlayVisible: %1").arg(force));
  if (!m_overlay) {
    return;
  }
  if (m_overlay->isForceVisible() == force) {
    return;
  }
  m_overlay->setForceVisible(force);
  refreshSelectionOverlayState();
}

void SelectionDisplayManager::handleHorizontalMoveAnimation(int selectedIndex, int prevIndex) {
  if (!m_overlay || !m_stateTracker || !m_activeWidgets) {
    return;
  }

  debugLog(QString("handleHorizontalMoveAnimation: prev=%1 sel=%2 forceVisible=%3")
               .arg(prevIndex)
               .arg(selectedIndex)
               .arg(m_overlay->isForceVisible()));

  // Calculate target rect for the new selection
  QRect targetRect = selectionOverlayRectForIndex(selectedIndex);
  if (!targetRect.isValid()) {
    if (m_ensureWidget) {
      m_ensureWidget(selectedIndex);
    }
    if (auto *w = m_activeWidgets->value(selectedIndex, nullptr)) {
      targetRect = SelectionOverlayManager::overlayRectForWidget(w);
    }
  }
  if (!targetRect.isValid()) {
    debugLog("  targetRect invalid, returning");
    return;
  }

  debugLog(QString("  targetRect: x=%1 y=%2").arg(targetRect.x()).arg(targetRect.y()));

  // Get start rect from previous selection if overlay not visible
  QRect startRect;
  if (!m_overlay->isVisible()) {
    startRect = selectionOverlayRectForIndex(prevIndex);
    if (!startRect.isValid()) {
      if (m_ensureWidget) {
        m_ensureWidget(prevIndex);
      }
      if (auto *w = m_activeWidgets->value(prevIndex, nullptr)) {
        startRect = SelectionOverlayManager::overlayRectForWidget(w);
      }
    }
  }

  // Update widget selection states
  int committedIdx = m_stateTracker->committedSelectedIndex();
  bool needsUpdate = m_stateTracker->needsCommitUpdate(selectedIndex);
  if (needsUpdate) {
    if (auto *prevWidget = m_activeWidgets->value(committedIdx, nullptr)) {
      prevWidget->setSelected(false);
    }
  }
  if (m_ensureWidget) {
    m_ensureWidget(selectedIndex);
  }
  if (auto *currWidget = m_activeWidgets->value(selectedIndex, nullptr)) {
    currWidget->setSelected(true);
  }
  m_stateTracker->commitSelection(selectedIndex);

  // Animate to target
  m_overlay->animateTo(targetRect, startRect);

  debugLog(QString("  animation started to (%1,%2)").arg(targetRect.x()).arg(targetRect.y()));
}

void SelectionDisplayManager::handleDirectSelectionUpdate(int selectedIndex) {
  if (!m_stateTracker || !m_activeWidgets) {
    return;
  }
  bool keepOverlay = m_overlay && m_overlay->shouldKeepVisible();

  if (m_overlay && m_overlay->isVisible()) {
    m_overlay->stopAnimation();
    if (!keepOverlay) {
      m_overlay->hide();
      if (m_state) {
        m_state->setGlideAnimating(false);
      }
    }
  }

  if (m_stateTracker->needsCommitUpdate(selectedIndex)) {
    if (auto *prevSel = m_activeWidgets->value(m_stateTracker->committedSelectedIndex(), nullptr)) {
      prevSel->setSelected(false);
    }
  }
  if (m_ensureWidget) {
    m_ensureWidget(selectedIndex);
  }
  auto *currSel = m_activeWidgets->value(selectedIndex, nullptr);
  if (currSel) {
    currSel->setSelected(true);
    // Force update to ensure border is drawn if we are in widget-border mode
    currSel->update();
  }

  if (keepOverlay) {
    // For direct updates (non-gliding) during click-hold (e.g. row wrap),
    // use widget border instead of overlay to avoid visual glitches.
    // This ensures we always have a visible selection indicator.
    if (m_state) {
      m_state->setGlideAnimating(false);
    }
    if (m_overlay) {
      m_overlay->hide();
      debugLog("  handleDirectSelectionUpdate: hiding overlay, using widget border");
    }
  }
  m_stateTracker->commitSelection(selectedIndex);
}

void SelectionDisplayManager::prewarmSurroundingWidgets(int selectedIndex) {
  if (!m_metrics || !m_ensureWidget || !m_totalItemsProvider) {
    return;
  }
  const int totalItems = m_totalItemsProvider();
  const int itemsPerRow = (m_metrics->itemsPerRow > 0 ? m_metrics->itemsPerRow : 1);
  const int prewarmRows = UIConstants::Grid::BUFFER_ROWS;
  const int halfWindow = prewarmRows * itemsPerRow;
  int start = std::max(0, selectedIndex - halfWindow);
  int end = std::min(totalItems - 1, selectedIndex + halfWindow);
  for (int visualIndex = start; visualIndex <= end; ++visualIndex) {
    m_ensureWidget(visualIndex);
  }
}

void SelectionDisplayManager::scheduleArrowKeyUpdate(int selectedIndex) {
  if (!m_arrowKeyScrollHelper || !m_arrowKeyViewUpdateTimer) {
    return;
  }

  if (m_state && m_state->scroll().hoverScrollPending) {
    return;
  }

  // Check if update should be skipped due to suppression
  if (m_arrowKeyScrollHelper->shouldSkipUpdate()) {
    return;
  }

  bool extendedHold = m_state && m_state->arrow().arrowKeyScrolling;
  static constexpr int ARROW_KEY_UPDATE_DELAY_EXTENDED_MS = 16;
  static constexpr int ARROW_KEY_UPDATE_DELAY_NORMAL_MS = 8;
  const int delayMs =
      extendedHold ? ARROW_KEY_UPDATE_DELAY_EXTENDED_MS : ARROW_KEY_UPDATE_DELAY_NORMAL_MS;
  m_arrowKeyViewUpdateTimer->start(delayMs);

  Q_UNUSED(selectedIndex)
}

void SelectionDisplayManager::updateSelectionForIndex(int selectedIndex) {
  if (!m_stateTracker || !m_metrics || !m_activeWidgets || !m_totalItemsProvider) {
    return;
  }
  const int totalItems = m_totalItemsProvider();
  const bool destroying = m_destroyingProvider && m_destroyingProvider();

  if (lcSelectionDisplay().isDebugEnabled()) {
    bool forceVisible = m_overlay && m_overlay->isForceVisible();
    debugLog(QString("updateSelectionForIndex: sel=%1 lastSel=%2 forceVisible=%3")
                 .arg(selectedIndex)
                 .arg(m_stateTracker->lastSelectedIndex())
                 .arg(forceVisible));
  }

  if (destroying || (!m_mediaScrollArea) || selectedIndex < 0 || selectedIndex >= totalItems) {
    debugLog("  early return due to invalid state");
    return;
  }

  int prevIndex = m_stateTracker->lastSelectedIndex();
  const bool sameSelection = (prevIndex == selectedIndex);

  if (!sameSelection) {
    updateSelectionDirection(selectedIndex, prevIndex);
  }

  prewarmSurroundingWidgets(selectedIndex);

  // Ensure widget exists and get reference
  if (m_ensureWidget) {
    m_ensureWidget(selectedIndex);
  }
  ItemWidget *currentWidget = m_activeWidgets->value(selectedIndex, nullptr);

  const bool keepOverlay = m_overlay && m_overlay->shouldKeepVisible();

  if (!currentWidget) {
    handleMissingWidgetSelection(selectedIndex, keepOverlay);
    return;
  }

  if (sameSelection) {
    handleSameSelectionUpdate(selectedIndex, currentWidget, keepOverlay);
    return;
  }

  handleNewSelectionUpdate(selectedIndex, prevIndex, currentWidget);
  scheduleArrowKeyUpdate(selectedIndex);
}

void SelectionDisplayManager::updateSelectionDirection(int selectedIndex, int prevIndex) {
  if (!m_stateTracker || !m_metrics) {
    return;
  }
  m_stateTracker->updateForNewSelection(selectedIndex, prevIndex, m_metrics->itemsPerRow);
  debugLog(QString("  updated lastSelectedIndex to %1").arg(selectedIndex));
}

void SelectionDisplayManager::handleMissingWidgetSelection(int selectedIndex, bool keepOverlay) {
  debugLog(QString("  currentWidget is null for index %1").arg(selectedIndex));
  // Widget not available, but during click-hold we still update overlay
  // position
  if (keepOverlay && m_overlay) {
    QRect rect = selectionOverlayRectForIndex(selectedIndex);
    if (rect.isValid()) {
      m_overlay->showAtRect(rect);
      debugLog(QString("  positioned overlay directly at (%1,%2)").arg(rect.x()).arg(rect.y()));
    }
  }
}

void SelectionDisplayManager::handleSameSelectionUpdate(int selectedIndex,
                                                        ItemWidget *currentWidget,
                                                        bool keepOverlay) {
  if (!m_stateTracker || !m_activeWidgets) {
    return;
  }
  debugLog("  same selection, returning");

  // If an animation is running, we MUST let it finish to achieve the "glide"
  // effect. Interrupting it here would cause the overlay to disappear and the
  // widget border to snap back immediately, defeating the purpose of the
  // animation.
  if (m_overlay && m_overlay->isAnimating()) {
    debugLog("  animation running during same-selection update - letting it "
             "continue");
    if (!m_overlay->isVisible()) {
      m_overlay->raise();
    }
    return;
  }

  if (keepOverlay && m_overlay) {
    m_overlay->showAtWidget(currentWidget);
    if (!m_overlay->isVisible()) {
      // Fallback to index-based positioning if widget-based failed
      QRect rect = selectionOverlayRectForIndex(selectedIndex);
      if (rect.isValid()) {
        m_overlay->showAtRect(rect);
      }
    }
  } else if (m_overlay && m_overlay->isVisible()) {
    m_overlay->hide();
  }

  if (m_stateTracker->needsCommitUpdate(selectedIndex)) {
    if (auto *prevSel = m_activeWidgets->value(m_stateTracker->committedSelectedIndex(), nullptr)) {
      prevSel->setSelected(false);
    }
  }
  currentWidget->setSelected(true);
  m_stateTracker->commitSelection(selectedIndex);

  if (m_state && !keepOverlay) {
    m_state->setGlideAnimating(false);
  }
  scheduleArrowKeyUpdate(selectedIndex);
}

void SelectionDisplayManager::handleNewSelectionUpdate(int selectedIndex, int prevIndex,
                                                       ItemWidget *currentWidget) {
  if (!m_metrics) {
    return;
  }
  // Use coordinator for movement analysis
  bool isHorizontalMove = false;
  if (m_selectionCoordinator) {
    auto movement =
        m_selectionCoordinator->analyzeMovement(selectedIndex, prevIndex, m_metrics->itemsPerRow);
    isHorizontalMove = movement.isHorizontal;
  } else {
    calculateMovementDirection(selectedIndex, prevIndex, m_metrics->itemsPerRow, isHorizontalMove);
  }

  debugLog(QString("  isHorizontalMove=%1").arg(isHorizontalMove));

  if (isHorizontalMove) {
    handleHorizontalMoveAnimation(selectedIndex, prevIndex);
  } else {
    handleDirectSelectionUpdate(selectedIndex);
  }

  Q_UNUSED(currentWidget)
}

void SelectionDisplayManager::calculateMovementDirection(int selectedIndex, int prevIndex,
                                                         int itemsPerRow, bool &isHorizontalMove) {
  isHorizontalMove = ScrollHelpers::movementDirection(selectedIndex, prevIndex, itemsPerRow);
}
