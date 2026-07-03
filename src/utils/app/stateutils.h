#ifndef STATEUTILS_H
#define STATEUTILS_H

#include <functional>

#include <QObject>
#include <QString>

class QTimer;
class VideoPreviewWidget;

/**
 * @brief Centralized selection restore state to replace scattered dynamic
 * properties.
 *
 * This struct provides a single source of truth for selection restoration
 * state, replacing the duplicated state previously maintained in
 * ViewportManager, SelectionManager, and EventManager, as well as various
 * PropertyKeys.
 *
 * Ownership: SelectionManager owns the canonical instance.
 * Access: Other managers query SelectionManager rather than maintaining copies.
 */
struct SelectionRestoreState {
  bool restoring = false;
  int targetIndex = -1;
  bool forceImmediateCenter = false;
  int restoreToken = 0;
  bool restorePending = false;
  bool userSelectionMade = false; // Set when user makes explicit selection, blocks auto-restore

  void reset() {
    restoring = false;
    targetIndex = -1;
    forceImmediateCenter = false;
    restorePending = false;
    userSelectionMade = false;
    // Note: restoreToken is not reset - it increments to invalidate old
    // operations
  }

  void cancel() {
    reset();
    ++restoreToken;
  }
};

/**
 * @brief Centralized scroll state to replace scattered dynamic properties.
 *
 * Consolidates scroll-related flags that were previously stored as
 * Qt dynamic properties via PropertyKeys.
 */
struct ScrollState {
  bool programmaticScroll = false;
  bool userScrollActive = false;
  bool userFreeScroll = false;
  bool clickScroll = false;
  bool clickContinuous = false;
  bool keyContinuous = false;
  bool horizHoldActive = false;
  bool clickHoldAdvancing = false;
  bool hoverScrollPending = false;

  void resetScrollFlags() {
    programmaticScroll = false;
    userScrollActive = false;
    userFreeScroll = false;
    clickScroll = false;
    clickContinuous = false;
    keyContinuous = false;
    horizHoldActive = false;
    clickHoldAdvancing = false;
    hoverScrollPending = false;
  }

  [[nodiscard]] bool isAnyContinuousActive() const {
    return clickContinuous || keyContinuous || clickHoldAdvancing || hoverScrollPending;
  }
};

/**
 * @brief Centralized artwork suppression state.
 *
 * Replaces PropertyKeys::SuppressArtwork and related properties.
 */
struct ArtworkState {
  bool suppressArtwork = false;
  bool allowDuringSelection = false;
  bool deferAllArtwork = false;
  bool deferUpdate = false;

  void reset() {
    suppressArtwork = false;
    allowDuringSelection = false;
    deferAllArtwork = false;
    deferUpdate = false;
  }
};

/**
 * @brief Centralized arrow key navigation state.
 *
 * Replaces arrow-key-related PropertyKeys.
 */
struct ArrowNavigationState {
  bool arrowKeyScrolling = false;
  bool suppressArrowCenter = false;
  qint64 suppressArrowCenterUntilMs = 0;

  void reset() {
    arrowKeyScrolling = false;
    suppressArrowCenter = false;
    suppressArrowCenterUntilMs = 0;
  }

  [[nodiscard]] bool isSuppressed(qint64 currentTimeMs) const {
    return suppressArrowCenter || currentTimeMs < suppressArrowCenterUntilMs;
  }
};

/**
 * @brief Centralized click handling state.
 *
 * Replaces click-related PropertyKeys for double-click detection and deferral.
 */
struct ClickState {
  int rowChangeFirstClickIndex = -1;
  qint64 rowChangeFirstClickMs = 0;
  bool doubleClickPending = false;
  int doubleClickPendingIndex = -1;
  bool clickDeferralActive = false;
  int clickDeferralIndex = -1;
  bool clickForceAnim = false;
  bool suppressInitialClickCenter = false;
  bool pendingInitialCenter = false;
  bool deferCenterOnClick = false;
  int deferredCenterIndex = -1;
  bool selectionSuppressed = false;
  int pendingSelectionIndex = -1;
  qint64 suppressDoubleClickUntilMs = 0;
  bool armFirstClickDelay = false;
  bool clickHoldRowChange = false;

  void reset() {
    rowChangeFirstClickIndex = -1;
    rowChangeFirstClickMs = 0;
    doubleClickPending = false;
    doubleClickPendingIndex = -1;
    clickDeferralActive = false;
    clickDeferralIndex = -1;
    clickForceAnim = false;
    suppressInitialClickCenter = false;
    pendingInitialCenter = false;
    deferCenterOnClick = false;
    deferredCenterIndex = -1;
    selectionSuppressed = false;
    pendingSelectionIndex = -1;
    suppressDoubleClickUntilMs = 0;
    armFirstClickDelay = false;
    clickHoldRowChange = false;
  }
};

/**
 * @brief Centralized streaming scroll state.
 *
 * Replaces streaming scroll-related PropertyKeys.
 */
struct StreamScrollState {
  bool streamVertical = false;
  int holdProgressPx = 0;
  double holdVelocityPxPerMs = 0.0;
  qint64 streamLastMs = 0;

  void reset() {
    streamVertical = false;
    holdProgressPx = 0;
    holdVelocityPxPerMs = 0.0;
    streamLastMs = 0;
  }
};

/**
 * @brief Centralized search bar state.
 *
 * Replaces PropertyKeys::ClearedByEscape which was previously stored
 * as a Qt dynamic property on the search bar widget.
 */
struct SearchState {
  bool clearedByEscape = false;

  void reset() { clearedByEscape = false; }
};

/**
 * @brief Centralized video preview playback state for DetailsPane.
 *
 * Groups the four scattered fields the pane needs to drive its
 * VideoPreviewWidget: the widget itself (raw pointer; parented to the
 * pane via Qt), the debounce timer that gates playVideo until scroll
 * settles, the queued path waiting for that timer, and the optional
 * scroll-idle predicate the timer consults before firing.
 *
 * Migration step toward a future VideoPlaybackController: the struct
 * is meant to be passed by const-ref into helpers that read state and
 * by ref into helpers that mutate it. Pointer ownership stays with
 * DetailsPane (Qt parent-child); the struct just groups the references.
 */
struct VideoPlaybackState {
  VideoPreviewWidget *videoPreview = nullptr;
  QTimer *videoStartTimer = nullptr;
  QString pendingVideoPath;
  std::function<bool()> scrollIdlePredicate;
};

#endif // STATEUTILS_H
