// Controls details pane visibility, positioning, and content updates.
#include "detailspanemanager.h"
#include "applicationcontext.h"
#include "collectionutils.h"
#include "detailspane.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "interactionstateholder.h"
#include "isettingsmanager.h"
#include "itemartwork.h"
#include "itemmetadata.h"
#include "pathutils.h"
#include "timerutils.h"
#include "uiconstants.h"
#include "videoutils.h"
#include <algorithm>
#include <QApplication>
#include <QFileInfo>
#include <QHash>
#include <QHBoxLayout>
#include <QList>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>

#include <QLoggingCategory>
// Default to warning — video / metadata lookups log on every grid
// selection move; with logging rules enabled that's a stderr flood
// during normal navigation. Opt in via
// `KARTEND_LOG_RULES=kartend.detailspanemanager.debug=true` to diagnose.
Q_LOGGING_CATEGORY(lcDetailsPaneManager, "kartend.detailspanemanager", QtWarningMsg)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcDetailsPaneManager().isDebugEnabled()) {                                                 \
      qCDebug(lcDetailsPaneManager) << msg;                                                        \
    }                                                                                              \
  } while (0)

// DetailsPaneManagerSetup getter definitions (non-manager fields only — sibling
// managers are read directly from ctx at runtime).
SETUP_GETTER_DEF_UI_SAME(DetailsPaneManagerSetup, DetailsPane *, Sidebar, sidebar)
SETUP_GETTER_DEF_UI_SAME(DetailsPaneManagerSetup, QWidget *, ItemsPage, itemsPage)
SETUP_GETTER_DEF_UI(DetailsPaneManagerSetup, QScrollArea *, ScrollArea, scrollArea, itemScrollArea)
SETUP_GETTER_DEF_COL_SAME(DetailsPaneManagerSetup, QList<CollectionConfig> *, Collections,
                          collections)

DetailsPaneManager::DetailsPaneManager(QObject *parent)
    : QObject(parent), m_DetailsPane(nullptr), m_itemsPage(nullptr),
      m_mainHorizontalLayout(nullptr), m_itemScrollArea(nullptr), m_currentCollectionIndex(-1) {}

void DetailsPaneManager::setupReferences(const DetailsPaneManagerSetup &setup) {
  m_ctx = setup.ctx;
  m_DetailsPane = setup.getSidebar();
  m_itemsPage = setup.getItemsPage();
  m_mainHorizontalLayout = setup.mainLayout;
  m_outerLayout = setup.outerLayout;
  m_mainContentWidget = setup.contentWidget;
  m_itemScrollArea = setup.getScrollArea();
  m_collections = setup.getCollections();
  m_runArtworkLinksDialog = setup.runArtworkLinksDialog;

  if (!m_metadataDebouncer) {
    m_metadataDebouncer =
        new TimerUtils::DebouncedTimer(UIConstants::DetailsPane::METADATA_DEBOUNCE_MS, this);
    connect(m_metadataDebouncer, &TimerUtils::DebouncedTimer::triggered, this, [this]() {
      const QString filePath = m_pendingMetadataFilePath;
      const QString itemName = m_pendingMetadataItemName;
      m_pendingMetadataFilePath.clear();
      m_pendingMetadataItemName.clear();
      performSidebarMetadataUpdate(filePath, itemName);
    });
  }

  // Forward a scroll-idle predicate to the sidebar so its video-preview
  // start timer can defer playVideo while a scroll animation is mid-glide.
  // playVideo's QMediaPlayer stop+setSource+play chain blocks the GUI
  // thread for ~100ms, which visibly stutters the still-running scroll
  // animation when the video debounce fires before the scroll settles
  // (Kartend-9q8d round 6). The predicate returns true (idle) only when
  // every scroll-state flag the InteractionStateHolder tracks is clear:
  //   - glideAnimating: smooth-scroll animation is mid-glide
  //   - programmaticScroll: an animation is currently driving the scrollbar
  //   - userScrollActive: user is actively engaging the wheel
  // ApplicationContext indirection keeps the predicate robust if
  // m_ctx->interactionState() is null (no plumbing → predicate degrades
  // to "always idle", preserving the pre-fix behaviour).
  if (m_DetailsPane) {
    const ApplicationContext *ctx = m_ctx;
    m_DetailsPane->setScrollIdlePredicate([ctx]() -> bool {
      if (!ctx) return true;
      const InteractionStateHolder *state = ctx->interactionState();
      if (!state) return true;
      if (state->glideAnimating()) return false;
      if (state->scroll().programmaticScroll) return false;
      if (state->scroll().userScrollActive) return false;
      return true;
    });
  }

  // Wire the per-item artwork-link editor. The sidebar
  // widget itself has no item context, so the manager handles the dialog.
  if (m_DetailsPane) {
    connect(m_DetailsPane, &DetailsPane::editArtworkRequested, this,
            &DetailsPaneManager::openArtworkLinksDialog);
    // bug #7: lower the sidebar while its own gallery overlay is
    // showing so the overlay (parented to the top-level window) stays on top.
    connect(m_DetailsPane, &DetailsPane::galleryOverlayVisibilityChanged, this,
            &DetailsPaneManager::setOverlayActive);
    // width drag: live resize via the inner-edge grip. Drag
    // events apply the width without persisting; commit on release writes
    // the final value to settings so a click without movement doesn't
    // touch the INI on every frame.
    connect(m_DetailsPane, &DetailsPane::widthDragged, this, [this](int width) {
      if (!m_DetailsPane) return;
      m_DetailsPane->setFixedWidth(width);
      // In overlay mode the sidebar is absolutely positioned — re-anchor so
      // the right edge stays pinned to the viewport (or the left edge in
      // Left position). In fixed mode the layout handles re-flow so we
      // skip the explicit position call.
      bool isFixedMode = false;
      if (m_collections && m_currentCollectionIndex >= 0 &&
          m_currentCollectionIndex < m_collections->size()) {
        isFixedMode =
            (*m_collections)[m_currentCollectionIndex].sidebarMode == DetailsPaneMode::Expand;
      }
      if (!isFixedMode) {
        positionSidebarOverlay();
      }
    });
    connect(m_DetailsPane, &DetailsPane::widthCommitted, this, [this](int width) {
      if (!m_collections || m_currentCollectionIndex < 0 ||
          m_currentCollectionIndex >= m_collections->size()) {
        return;
      }
      (*m_collections)[m_currentCollectionIndex].sidebarWidth = width;
      if (auto *sm = m_ctx ? m_ctx->settingsManager() : nullptr) {
        sm->saveCollections(*m_collections);
      }
    });
    // height-drag handlers for Top/Bottom dock. Mirror the width
    // pair — live setFixedHeight() during the drag, persist on commit. In
    // Overlay mode the pane is absolutely positioned so we re-anchor; in
    // Expand mode the outer QVBoxLayout reflows automatically.
    connect(m_DetailsPane, &DetailsPane::heightDragged, this, [this](int height) {
      if (!m_DetailsPane) return;
      m_DetailsPane->setFixedHeight(height);
      bool isFixedMode = false;
      if (m_collections && m_currentCollectionIndex >= 0 &&
          m_currentCollectionIndex < m_collections->size()) {
        isFixedMode =
            (*m_collections)[m_currentCollectionIndex].sidebarMode == DetailsPaneMode::Expand;
      }
      if (!isFixedMode) {
        positionSidebarOverlay();
      }
    });
    connect(m_DetailsPane, &DetailsPane::heightCommitted, this, [this](int height) {
      if (!m_collections || m_currentCollectionIndex < 0 ||
          m_currentCollectionIndex >= m_collections->size()) {
        return;
      }
      (*m_collections)[m_currentCollectionIndex].sidebarHeight = height;
      if (auto *sm = m_ctx ? m_ctx->settingsManager() : nullptr) {
        sm->saveCollections(*m_collections);
      }
    });
    // tabs: persist the user's tab choice per collection, then re-push the
    // current selection so the new tab gets a fresh population pass — each
    // tab now owns a distinct widget set, and the per-item setters gate
    // their show()/hide() on the active tab, so a one-shot tab switch
    // wouldn't see the data without this re-run.
    connect(m_DetailsPane, &DetailsPane::activeTabChanged, this, [this](DetailsPaneTab tab) {
      if (!m_collections || m_currentCollectionIndex < 0 ||
          m_currentCollectionIndex >= m_collections->size()) {
        return;
      }
      (*m_collections)[m_currentCollectionIndex].sidebarActiveTab = tab;
      if (auto *sm = m_ctx ? m_ctx->settingsManager() : nullptr) {
        sm->saveCollections(*m_collections);
      }
      if (!m_currentItemFilePath.isEmpty()) {
        // Tab switch is a deliberate user action — refresh immediately so
        // the newly-selected tab's content paints without the debounce
        // latency that selection-driven updates accept.
        refreshSidebarMetadataImmediate();
      }
    });
  }
}

void DetailsPaneManager::toggleSidebar() {
  if (!m_DetailsPane) {
    return;
  }

  // a deliberate user toggle outranks the cover-flow auto-hide
  // so the user can pull the sidebar back in even while in cover flow.
  m_externallyHidden = false;
  m_sidebarVisible = !m_sidebarVisible;
  updateSidebarLayout(m_currentCollectionIndex);
  emit sidebarVisibilityChanged(m_sidebarVisible);
}

void DetailsPaneManager::setExternallyHidden(bool hidden) {
  if (m_externallyHidden == hidden) {
    return;
  }
  m_externallyHidden = hidden;
  // Re-run layout: when forced hidden, the sidebar is removed from the
  // layout regardless of m_sidebarVisible; when the override clears, the
  // layout falls back to the persisted m_sidebarVisible state. Persistence
  // is suppressed inside updateSidebarLayout while m_externallyHidden is
  // true so the user's per-collection preference survives the round trip.
  updateSidebarLayout(m_currentCollectionIndex);
  emit sidebarVisibilityChanged(m_sidebarVisible && !m_externallyHidden);
}

void DetailsPaneManager::applySidebarStateForCollection(int collectionIndex) {
  if (!m_collections || collectionIndex < 0 || collectionIndex >= m_collections->size()) {
    return;
  }

  m_currentCollectionIndex = collectionIndex;
  const CollectionConfig &collection = (*m_collections)[collectionIndex];
  m_sidebarVisible = collection.sidebarVisible;

  // apply per-collection appearance (bg type, colors, pattern).
  if (m_DetailsPane) {
    m_DetailsPane->applyAppearance(collection);
  }

  // Push the new collection's summary to the sidebar so the no-selection
  // state shows useful context.
  refreshCollectionSummary();

  updateSidebarLayout(collectionIndex);
  emit sidebarVisibilityChanged(m_sidebarVisible);

  // Reposition overlay sidebar after layout is finalized - on startup, the
  // viewport geometry may not be fully set when this is first called, causing
  // the sidebar to overlap the scrollbar. Deferring ensures correct
  // positioning.: only reposition for Overlay mode — Fixed mode
  // is now in m_mainHorizontalLayout and absolute-positioning it would
  // wreck the layout dock.
  if (m_sidebarVisible && collection.sidebarMode != DetailsPaneMode::Expand) {
    QTimer::singleShot(50, this, [this]() { positionSidebarOverlay(); });
  }
}

void DetailsPaneManager::setupSidebar() {
  m_sidebarVisible = false;
}

void DetailsPaneManager::refreshCollectionSummary() {
  if (!m_DetailsPane) {
    return;
  }
  if (!m_collections || m_currentCollectionIndex < 0 ||
      m_currentCollectionIndex >= m_collections->size()) {
    m_DetailsPane->setCollectionSummary({});
    return;
  }

  const CollectionConfig &collection = (*m_collections)[m_currentCollectionIndex];
  DetailsPane::CollectionSummary summary;
  summary.name = collection.name;
  summary.type = CollectionUtils::effectiveCollectionType(m_currentCollectionIndex, *m_collections);
  summary.extensions = collection.extensions;

  // Render expanded paths (with %collection% / ~ resolved) so the user sees
  // the real filesystem location. validateAndExpandPath returns "" when the
  // resolved directory does not exist; fall back to the raw template in
  // that case so the user can still see what was configured.
  auto expandOrRaw = [&](const QString &raw) -> QString {
    if (raw.trimmed().isEmpty()) {
      return {};
    }
    const QString expanded = PathUtils::validateAndExpandPath(raw, collection.name);
    return expanded.isEmpty() ? raw : expanded;
  };
  summary.mediaDirectory = expandOrRaw(collection.mediaDirectory);
  summary.artworkDirectory = expandOrRaw(collection.artworkDirectory);
  summary.videoDirectory = expandOrRaw(collection.videoDirectory);
  summary.manualDirectory = expandOrRaw(collection.manualDirectory);

  if (collection.isSubcollection && collection.parentCollectionIndex >= 0 &&
      collection.parentCollectionIndex < m_collections->size()) {
    summary.parentName = (*m_collections)[collection.parentCollectionIndex].name;
  }

  if (auto *db = m_ctx ? m_ctx->databaseManager() : nullptr; db) {
    summary.itemCount = db->countCollectionRecursive(m_currentCollectionIndex, *m_collections);
    // UUID keying must match how DatabaseManager computes it elsewhere:
    // validateAndExpandPath without a raw fallback. Mismatched casing or
    // a missing-directory empty-string here would make last_scanned silently
    // miss its row.
    const QString uuidMediaDir =
        PathUtils::validateAndExpandPath(collection.mediaDirectory, collection.name);
    const QString uuid = CollectionUtils::computeCollectionUuid(collection.name, uuidMediaDir);
    summary.lastScanned = db->loadCollectionLastScanned(uuid);
  }

  m_DetailsPane->setCollectionSummary(summary);
}

void DetailsPaneManager::positionSidebarOverlay() {
  if (!m_DetailsPane || !m_itemsPage) {
    return;
  }

  const int sidebarMargin = UIConstants::DetailsPane::MARGIN;

  // Read position + size from the active collection so the overlay anchors on
  // the correct edge.
  DetailsPanePosition position = DetailsPanePosition::Right;
  int desiredWidth = UIConstants::DetailsPane::FIXED_WIDTH;
  int desiredHeight = UIConstants::DetailsPane::FIXED_HEIGHT;
  if (m_collections && m_currentCollectionIndex >= 0 &&
      m_currentCollectionIndex < m_collections->size()) {
    const CollectionConfig &collection = (*m_collections)[m_currentCollectionIndex];
    position = collection.sidebarPosition;
    desiredWidth = std::max(collection.sidebarWidth, UIConstants::DetailsPane::MIN_WIDTH);
    desiredHeight = std::max(collection.sidebarHeight, UIConstants::DetailsPane::MIN_HEIGHT);
  }

  QRect viewportRectInItems;
  if (m_itemScrollArea && m_itemScrollArea->viewport()) {
    const QPoint topLeft = m_itemScrollArea->viewport()->mapTo(m_itemsPage, QPoint(0, 0));
    viewportRectInItems = QRect(topLeft, m_itemScrollArea->viewport()->size());
  } else {
    viewportRectInItems = m_itemsPage->rect();
  }

  // anchor flush with the viewport edge — earlier code subtracted
  // a full scrollbar width to keep the bar visible to the right of the
  // sidebar, but the user wants the gap minimized.
  // Top/Bottom dock spans the full viewport perpendicular to
  // the dock edge (i.e. full width, fixed height).
  int x = viewportRectInItems.left() + sidebarMargin;
  int y = viewportRectInItems.top() + sidebarMargin;
  int w = qMax(0, viewportRectInItems.width() - (sidebarMargin * 2));
  int h = qMax(0, viewportRectInItems.height() - (sidebarMargin * 2));

  switch (position) {
  case DetailsPanePosition::Left:
    w = qMin(desiredWidth, viewportRectInItems.width() - sidebarMargin);
    break;
  case DetailsPanePosition::Right:
    w = qMin(desiredWidth, viewportRectInItems.width() - sidebarMargin);
    x = viewportRectInItems.left() + viewportRectInItems.width() - w - sidebarMargin;
    break;
  case DetailsPanePosition::Top:
    h = qMin(desiredHeight, viewportRectInItems.height() - sidebarMargin);
    break;
  case DetailsPanePosition::Bottom:
    h = qMin(desiredHeight, viewportRectInItems.height() - sidebarMargin);
    y = viewportRectInItems.top() + viewportRectInItems.height() - h - sidebarMargin;
    break;
  }

  m_DetailsPane->setGeometry(x, y, w, h);
  // Skip raise() while a fullscreen overlay (artwork preview) is showing —
  // the overlay needs to stay above the sidebar (bug #7).
  if (!m_overlayActive) {
    m_DetailsPane->raise();
  }
}

void DetailsPaneManager::setOverlayActive(bool active) {
  if (m_overlayActive == active) {
    return;
  }
  m_overlayActive = active;
  // When the overlay closes, re-stack the sidebar so it sits above its
  // siblings again. When it opens, lower the sidebar so the overlay
  // (which is reparented to the top-level window and raise()'d on show)
  // can cover it cleanly.
  if (m_DetailsPane) {
    if (active) {
      // bug #5: silence the sidebar's looping preview video
      // before the overlay starts its own — without this, two video tracks
      // play simultaneously and audio from the muted-but-still-decoding
      // sidebar player can leak under the overlay's playback. Soft-pause
      // keeps the source loaded so resume can pick up at the same spot.
      m_DetailsPane->pausePreviewVideo();
      m_DetailsPane->lower();
    } else {
      m_DetailsPane->raise();
      // Restore the looping preview now that the overlay is gone — without
      // this the sidebar would be stuck on the static artwork frame even
      // when the item has a video.
      m_DetailsPane->resumePreviewVideo();
    }
  }
}

void DetailsPaneManager::updateSidebarLayout(int currentCollectionIndex) {
  if (!m_DetailsPane || !m_mainHorizontalLayout) {
    return;
  }

  bool isFixedMode = false;
  DetailsPanePosition position = DetailsPanePosition::Right;
  int desiredWidth = UIConstants::DetailsPane::FIXED_WIDTH;
  int desiredHeight = UIConstants::DetailsPane::FIXED_HEIGHT;
  if (m_collections && currentCollectionIndex >= 0 &&
      currentCollectionIndex < m_collections->size()) {
    const CollectionConfig &collection = (*m_collections)[currentCollectionIndex];
    isFixedMode = (collection.sidebarMode == DetailsPaneMode::Expand);
    position = collection.sidebarPosition;
    desiredWidth = std::max(collection.sidebarWidth, UIConstants::DetailsPane::MIN_WIDTH);
    desiredHeight = std::max(collection.sidebarHeight, UIConstants::DetailsPane::MIN_HEIGHT);
  }

  // tracking "in some layout" rather than just the horizontal
  // one — Top/Bottom Expand puts the pane in m_outerLayout instead.
  auto inAnyLayout = [this]() {
    if (m_mainHorizontalLayout && m_mainHorizontalLayout->indexOf(m_DetailsPane) != -1) {
      return true;
    }
    if (m_outerLayout && m_outerLayout->indexOf(m_DetailsPane) != -1) {
      return true;
    }
    return false;
  };
  auto removeFromLayouts = [this]() {
    if (m_mainHorizontalLayout && m_mainHorizontalLayout->indexOf(m_DetailsPane) != -1) {
      m_mainHorizontalLayout->removeWidget(m_DetailsPane);
    }
    if (m_outerLayout && m_outerLayout->indexOf(m_DetailsPane) != -1) {
      m_outerLayout->removeWidget(m_DetailsPane);
    }
  };
  // swap the size policy so a Top/Bottom-docked pane stretches
  // horizontally with a fixed height, while a Left/Right-docked pane stretches
  // vertically with a fixed width. Without this swap the .ui's Fixed/Expanding
  // hsizetype/vsizetype would pin the pane to a narrow column even after we
  // try to dock it across the top.
  auto applyDockSizing = [this, position, desiredWidth, desiredHeight]() {
    if (!m_DetailsPane) return;
    if (CollectionUtils::isDetailsPaneHorizontal(position)) {
      m_DetailsPane->setMinimumWidth(0);
      m_DetailsPane->setMaximumWidth(QWIDGETSIZE_MAX);
      m_DetailsPane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      m_DetailsPane->setFixedHeight(desiredHeight);
    } else {
      m_DetailsPane->setMinimumHeight(0);
      m_DetailsPane->setMaximumHeight(QWIDGETSIZE_MAX);
      m_DetailsPane->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
      m_DetailsPane->setFixedWidth(desiredWidth);
    }
  };

  bool wasInLayout = inAnyLayout();
  m_DetailsPane->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // cover flow takes the full viewport — render as if
  // m_sidebarVisible were false but skip the persistence step below so the
  // user's per-collection preference is preserved across view-type cycles.
  const bool effectiveVisible = m_sidebarVisible && !m_externallyHidden;
  if (effectiveVisible) {
    if (isFixedMode) {
      removeFromLayouts();
      applyDockSizing();
      // bug #3 fix /: Fixed mode docks into a Qt
      // layout so the grid reflows into the remaining space. Left/Right go
      // into the existing horizontal layout; Top/Bottom go into the outer
      // vertical layout (above or below m_mainContentWidget). When the outer
      // layout isn't wired up, fall back to the horizontal layout for L/R
      // and to absolute positioning for T/B (matches Overlay).
      if (CollectionUtils::isDetailsPaneHorizontal(position) && m_outerLayout &&
          m_mainContentWidget) {
        const int contentIndex = m_outerLayout->indexOf(m_mainContentWidget);
        if (position == DetailsPanePosition::Top) {
          const int insertAt = contentIndex >= 0 ? contentIndex : 0;
          m_outerLayout->insertWidget(insertAt, m_DetailsPane);
        } else {
          // Bottom: insert after m_mainContentWidget. addWidget puts it at the
          // end; if there are trailing items in the outer layout we'd want a
          // proper index, but in practice the content widget is the last entry.
          if (contentIndex >= 0) {
            m_outerLayout->insertWidget(contentIndex + 1, m_DetailsPane);
          } else {
            m_outerLayout->addWidget(m_DetailsPane);
          }
        }
      } else if (position == DetailsPanePosition::Left) {
        m_mainHorizontalLayout->insertWidget(0, m_DetailsPane);
      } else if (position == DetailsPanePosition::Right) {
        m_mainHorizontalLayout->addWidget(m_DetailsPane);
      } else {
        // Top/Bottom Expand without an outer layout: degrade to overlay.
        m_DetailsPane->setParent(m_itemsPage);
        positionSidebarOverlay();
      }
      m_DetailsPane->setVisible(true);
    } else {
      // Overlay mode: free-floating child of itemsPage so the pane visually
      // overlaps the grid without consuming layout space. The geometry call
      // in positionSidebarOverlay handles all four positions.
      m_DetailsPane->setParent(m_itemsPage);
      removeFromLayouts();
      applyDockSizing();
      positionSidebarOverlay();
      m_DetailsPane->setVisible(true);
    }
  } else {
    m_DetailsPane->setVisible(false);
    removeFromLayouts();
  }

  bool isNowInLayout = inAnyLayout();
  if (wasInLayout != isNowInLayout) {
    emit sidebarVisibilityChanged(effectiveVisible);
  }

  // force layout reflow when the dock target changed mid-call
  // (e.g. user just flipped Position from Right to Top in settings — the
  // widget moves between m_mainHorizontalLayout and m_outerLayout, but Qt
  // won't repaint the new placement unless the affected layouts are
  // invalidated and the widget's size hint is re-queried).
  m_DetailsPane->updateGeometry();
  if (m_outerLayout) {
    m_outerLayout->invalidate();
    m_outerLayout->activate();
  }
  if (m_mainHorizontalLayout) {
    m_mainHorizontalLayout->invalidate();
    m_mainHorizontalLayout->activate();
  }

  // Skip persistence when the override is what's hiding the sidebar — this
  // is a transient view-driven state, not a user-initiated choice.
  if (currentCollectionIndex >= 0 && !m_externallyHidden) {
    saveSidebarStateForCollection(currentCollectionIndex, m_sidebarVisible);
  }

  if (auto *art = m_ctx ? m_ctx->artworkManager() : nullptr) {
    if (auto *timerCoordinator = art->getTimerCoordinator()) {
      timerCoordinator->scheduleLayoutUpdate();
    }
  }

  // Delay sidebar layout changed signal to allow show/hide animation
  // to start before other components react to the layout change
  QTimer::singleShot(UIConstants::DetailsPane::LAYOUT_NOTIFY_DELAY_MS, this,
                     [this]() { emit sidebarLayoutChanged(); });
}

auto DetailsPaneManager::isSidebarVisible() const -> bool {
  return m_sidebarVisible;
}

// Persists the sidebar visibility state for a collection index and writes
// settings
void DetailsPaneManager::saveSidebarStateForCollection(int collectionIndex, bool visible) {
  if (!m_collections || collectionIndex < 0 || collectionIndex >= m_collections->size()) {
    return;
  }
  (*m_collections)[collectionIndex].sidebarVisible = visible;
  if (auto *sm = m_ctx ? m_ctx->settingsManager() : nullptr) {
    sm->saveCollections(*m_collections);
  }
}

// Persists the sidebar visibility state by collection name, forwarding to
// index-based save
void DetailsPaneManager::saveSidebarStateForCollection(const QString &collectionName,
                                                       bool visible) {
  if (!m_collections) {
    return;
  }
  int idx = -1;
  for (int i = 0; i < m_collections->size(); ++i) {
    if ((*m_collections)[i].name == collectionName) {
      idx = i;
      break;
    }
  }
  if (idx < 0) {
    return;
  }
  saveSidebarStateForCollection(idx, visible);
}