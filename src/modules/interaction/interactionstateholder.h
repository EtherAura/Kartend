#ifndef INTERACTIONSTATEHOLDER_H
#define INTERACTIONSTATEHOLDER_H

#include "stateutils.h"
#include <QDateTime>
#include <QObject>
#include <QThread>

/**
 * @brief Centralized state holder for interaction-related flags.
 *
 * Replaces scattered Qt dynamic properties with typed, discoverable state.
 * This class owns the canonical state structs from stateutils.h and provides
 * a single point of access for managers that need to read/write interaction
 * state.
 *
 * Usage:
 *   // In InteractionManager or ApplicationContext
 *   InteractionStateHolder m_state;
 *
 *   // Reading state
 *   if (m_state.scroll().programmaticScroll) { ... }
 *
 *   // Writing state
 *   m_state.scroll().programmaticScroll = true;
 *
 *   // Using helper methods
 *   m_state.beginProgrammaticScroll();
 *   // ... do scroll ...
 *   m_state.endProgrammaticScroll();
 *
 * ─────────────────────────────────────────────────────────────────────────
 * THREAD-SAFETY CONTRACT (main-thread-only)
 * ─────────────────────────────────────────────────────────────────────────
 * InteractionStateHolder is NOT thread-safe and is intentionally so. All
 * reads and writes MUST occur on the thread that owns this QObject (the
 * GUI/main thread in Kartend's architecture).
 *
 * Rationale: every existing consumer (InteractionManager, EventManager,
 * MouseManager, KeyboardManager, SearchManager, SelectionOverlayManager,
 * ArrowNavigationHandler, ArtworkManager) inspects state from main-thread
 * slots, timers, or event handlers. Worker threads (QtConcurrent batches in
 * ArtworkManager, the QueryManager DB worker) deliberately operate on
 * value-typed inputs and post results back via QMetaObject::invokeMethod or
 * queued signals — they MUST NOT call into this holder directly.
 *
 * If you need a flag inside a worker lambda, snapshot it on the main thread
 * before dispatch and capture by value, OR introduce a dedicated atomic /
 * mutex-guarded mirror. Do NOT silently start touching this holder from a
 * worker thread.
 *
 * Debug builds enforce this via assertOwnerThread() in mutating helpers.
 * Release builds rely on the contract documented here.
 */
class InteractionStateHolder : public QObject {
  Q_OBJECT

public:
  explicit InteractionStateHolder(QObject *parent = nullptr);
  ~InteractionStateHolder() override = default;

  // ─────────────────────────────────────────────────────────────────────────
  // State struct accessors (non-const for read/write access)
  // ─────────────────────────────────────────────────────────────────────────

  [[nodiscard]] SelectionRestoreState &selectionRestore() {
    return m_selectionRestore;
  }
  [[nodiscard]] const SelectionRestoreState &selectionRestore() const {
    return m_selectionRestore;
  }

  [[nodiscard]] ScrollState &scroll() { return m_scroll; }
  [[nodiscard]] const ScrollState &scroll() const { return m_scroll; }

  [[nodiscard]] ArtworkState &artwork() { return m_artwork; }
  [[nodiscard]] const ArtworkState &artwork() const { return m_artwork; }

  [[nodiscard]] ArrowNavigationState &arrow() { return m_arrow; }
  [[nodiscard]] const ArrowNavigationState &arrow() const { return m_arrow; }

  [[nodiscard]] ClickState &click() { return m_click; }
  [[nodiscard]] const ClickState &click() const { return m_click; }

  [[nodiscard]] StreamScrollState &streamScroll() { return m_streamScroll; }
  [[nodiscard]] const StreamScrollState &streamScroll() const {
    return m_streamScroll;
  }

  [[nodiscard]] SearchState &search() { return m_search; }
  [[nodiscard]] const SearchState &search() const { return m_search; }

  // ─────────────────────────────────────────────────────────────────────────
  // Additional state not in stateutils.h structs
  // ─────────────────────────────────────────────────────────────────────────

  [[nodiscard]] bool glideAnimating() const { return m_glideAnimating; }
  void setGlideAnimating(bool active) { m_glideAnimating = active; }

  [[nodiscard]] bool horizAnimActive() const { return m_horizAnimActive; }
  void setHorizAnimActive(bool active) { m_horizAnimActive = active; }

  [[nodiscard]] int horizAnimGen() const { return m_horizAnimGen; }
  void setHorizAnimGen(int gen) { m_horizAnimGen = gen; }
  int nextHorizAnimGen() { return ++m_horizAnimGen; }

  [[nodiscard]] qint64 clickSeriesLastMs() const { return m_clickSeriesLastMs; }
  void setClickSeriesLastMs(qint64 ms) { m_clickSeriesLastMs = ms; }
  void updateClickSeriesLastMs() {
    m_clickSeriesLastMs = QDateTime::currentMSecsSinceEpoch();
  }

  [[nodiscard]] qint64 lastUiActivityMs() const { return m_lastUiActivityMs; }
  void setLastUiActivityMs(qint64 ms) { m_lastUiActivityMs = ms; }
  void updateLastUiActivity() {
    m_lastUiActivityMs = QDateTime::currentMSecsSinceEpoch();
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Convenience methods for common state transitions
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Begin a programmatic scroll operation.
   * Sets programmaticScroll flag and clears userScrollActive.
   */
  void beginProgrammaticScroll() {
    assertOwnerThread();
    m_scroll.programmaticScroll = true;
    m_scroll.userScrollActive = false;
  }

  /**
   * @brief End a programmatic scroll operation.
   */
  void endProgrammaticScroll() {
    assertOwnerThread();
    m_scroll.programmaticScroll = false;
  }

  /**
   * @brief Suppress arrow centering for a duration.
   * @param durationMs How long to suppress in milliseconds.
   */
  void suppressArrowCenterFor(qint64 durationMs) {
    assertOwnerThread();
    m_arrow.suppressArrowCenter = true;
    m_arrow.suppressArrowCenterUntilMs =
        QDateTime::currentMSecsSinceEpoch() + durationMs;
  }

  /**
   * @brief Check if arrow centering is currently suppressed.
   */
  [[nodiscard]] bool isArrowCenterSuppressed() const {
    return m_arrow.isSuppressed(QDateTime::currentMSecsSinceEpoch());
  }

  /**
   * @brief Clear arrow center suppression.
   */
  void clearArrowCenterSuppression() {
    assertOwnerThread();
    m_arrow.suppressArrowCenter = false;
    m_arrow.suppressArrowCenterUntilMs = 0;
  }

  /**
   * @brief Begin selection suppression for deferred selection.
   * @param pendingIndex The index to select when suppression ends.
   */
  void beginSelectionSuppression(int pendingIndex) {
    assertOwnerThread();
    m_click.selectionSuppressed = true;
    m_click.pendingSelectionIndex = pendingIndex;
  }

  /**
   * @brief End selection suppression and get pending index.
   * @return The pending selection index, or -1 if none.
   */
  int endSelectionSuppression() {
    assertOwnerThread();
    int pending = m_click.pendingSelectionIndex;
    m_click.selectionSuppressed = false;
    m_click.pendingSelectionIndex = -1;
    return pending;
  }

  /**
   * @brief Check if selection is currently suppressed.
   */
  [[nodiscard]] bool isSelectionSuppressed() const {
    return m_click.selectionSuppressed;
  }

  /**
   * @brief Get the pending selection index.
   */
  [[nodiscard]] int pendingSelectionIndex() const {
    return m_click.pendingSelectionIndex;
  }

  /**
   * @brief Reset all state to defaults.
   */
  void resetAll() {
    assertOwnerThread();
    m_selectionRestore.reset();
    m_scroll.resetScrollFlags();
    m_artwork.reset();
    m_arrow.reset();
    m_click.reset();
    m_streamScroll.reset();
    m_search.reset();
    m_glideAnimating = false;
    m_horizAnimActive = false;
    m_horizAnimGen = 0;
    m_clickSeriesLastMs = 0;
    m_lastUiActivityMs = 0;
  }

signals:
  /**
   * @brief Emitted when glide animation state changes.
   */
  void glideAnimatingChanged(bool active);

  /**
   * @brief Emitted when programmatic scroll state changes.
   */
  void programmaticScrollChanged(bool active);

  /**
   * @brief Emitted when selection suppression state changes.
   */
  void selectionSuppressionChanged(bool suppressed, int pendingIndex);

private:
  // Debug-only thread-affinity assertion. Confirms the caller is on the
  // QObject's owning thread (typically the GUI/main thread). No-op in
  // release builds. See the THREAD-SAFETY CONTRACT block at the top of this
  // header.
  void assertOwnerThread() const {
    Q_ASSERT_X(thread() == QThread::currentThread(),
               "InteractionStateHolder",
               "InteractionStateHolder accessed from a non-owner thread; "
               "see header THREAD-SAFETY CONTRACT.");
  }

  // State structs from stateutils.h
  SelectionRestoreState m_selectionRestore;
  ScrollState m_scroll;
  ArtworkState m_artwork;
  ArrowNavigationState m_arrow;
  ClickState m_click;
  StreamScrollState m_streamScroll;
  SearchState m_search;

  // Additional state
  bool m_glideAnimating = false;
  bool m_horizAnimActive = false;
  int m_horizAnimGen = 0;
  qint64 m_clickSeriesLastMs = 0;
  qint64 m_lastUiActivityMs = 0;
};

#endif // INTERACTIONSTATEHOLDER_H
