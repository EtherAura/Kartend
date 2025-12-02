#ifndef INTERACTIONSTATEHOLDER_H
#define INTERACTIONSTATEHOLDER_H

#include "stateutils.h"
#include <QDateTime>
#include <QObject>

/**
 * @brief Centralized state holder for interaction-related flags.
 *
 * Replaces scattered Qt dynamic properties with typed, discoverable state.
 * This class owns the canonical state structs from stateutils.h and provides
 * a single point of access for managers that need to read/write interaction state.
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
 */
class InteractionStateHolder : public QObject {
  Q_OBJECT

public:
  explicit InteractionStateHolder(QObject *parent = nullptr);
  ~InteractionStateHolder() override = default;

  // ─────────────────────────────────────────────────────────────────────────
  // State struct accessors (non-const for read/write access)
  // ─────────────────────────────────────────────────────────────────────────

  [[nodiscard]] SelectionRestoreState &selectionRestore() { return m_selectionRestore; }
  [[nodiscard]] const SelectionRestoreState &selectionRestore() const { return m_selectionRestore; }

  [[nodiscard]] ScrollState &scroll() { return m_scroll; }
  [[nodiscard]] const ScrollState &scroll() const { return m_scroll; }

  [[nodiscard]] ArtworkState &artwork() { return m_artwork; }
  [[nodiscard]] const ArtworkState &artwork() const { return m_artwork; }

  [[nodiscard]] ArrowNavigationState &arrow() { return m_arrow; }
  [[nodiscard]] const ArrowNavigationState &arrow() const { return m_arrow; }

  [[nodiscard]] ClickState &click() { return m_click; }
  [[nodiscard]] const ClickState &click() const { return m_click; }

  [[nodiscard]] StreamScrollState &streamScroll() { return m_streamScroll; }
  [[nodiscard]] const StreamScrollState &streamScroll() const { return m_streamScroll; }

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
  void updateClickSeriesLastMs() { m_clickSeriesLastMs = QDateTime::currentMSecsSinceEpoch(); }

  [[nodiscard]] qint64 lastUiActivityMs() const { return m_lastUiActivityMs; }
  void setLastUiActivityMs(qint64 ms) { m_lastUiActivityMs = ms; }
  void updateLastUiActivity() { m_lastUiActivityMs = QDateTime::currentMSecsSinceEpoch(); }

  // ─────────────────────────────────────────────────────────────────────────
  // Convenience methods for common state transitions
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Begin a programmatic scroll operation.
   * Sets programmaticScroll flag and clears userScrollActive.
   */
  void beginProgrammaticScroll() {
    m_scroll.programmaticScroll = true;
    m_scroll.userScrollActive = false;
  }

  /**
   * @brief End a programmatic scroll operation.
   */
  void endProgrammaticScroll() {
    m_scroll.programmaticScroll = false;
  }

  /**
   * @brief Suppress arrow centering for a duration.
   * @param durationMs How long to suppress in milliseconds.
   */
  void suppressArrowCenterFor(qint64 durationMs) {
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
    m_arrow.suppressArrowCenter = false;
    m_arrow.suppressArrowCenterUntilMs = 0;
  }

  /**
   * @brief Begin selection suppression for deferred selection.
   * @param pendingIndex The index to select when suppression ends.
   */
  void beginSelectionSuppression(int pendingIndex) {
    m_click.selectionSuppressed = true;
    m_click.pendingSelectionIndex = pendingIndex;
  }

  /**
   * @brief End selection suppression and get pending index.
   * @return The pending selection index, or -1 if none.
   */
  int endSelectionSuppression() {
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
