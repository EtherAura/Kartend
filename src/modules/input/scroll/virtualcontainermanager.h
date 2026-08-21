#ifndef VIRTUALCONTAINERMANAGER_H
#define VIRTUALCONTAINERMANAGER_H

#include "collectiontypes.h"
#include <QObject>
#include <QPointer>

class QWidget;
class QScrollArea;
class QScrollBar;
class SelectionOverlayManager;
class FilterManager;
struct GridMetrics;

/**
 * @brief Parameters for container positioning calculations.
 */
struct ContainerPositionParams {
  int totalWidth = 0;
  int totalHeight = 0;
  int itemsPerRow = 0;
  int totalItems = 0;
  HorizontalAlignment alignment = HorizontalAlignment::Center;
  bool isFiltered = false;
  /// when true, the items area scrolls along x. The container
  /// keeps its full width (no overflow-clamping or center-shift) and the
  /// horizontal scrollbar is left enabled instead of force-hidden.
  bool isHorizontal = false;
  /// Dead width on EACH side of the content block: the part of the first and
  /// last cell that the artwork never paints. The art box is a square capped
  /// by the cell's HEIGHT, so a cell wider than that carries blank margin
  /// (measured 2026-08-19: 47px per side on a 200px cell holding 106px art).
  /// Alignment anchors what the user can SEE, so it works in painted
  /// coordinates and this is how it gets there. Zero leaves the old
  /// cell-box behaviour exactly as it was.
  ///
  /// NEGATIVE means "not measured this pass", which is NOT the same as zero.
  /// The value is read off a materialized cell, and the pool is momentarily
  /// empty during a relayout — so a resize produced the sequence 47, 0, 47.
  /// Read as real inset changes those looked like layout changes and threw
  /// away the held position, which is why the grid still jumped when the
  /// details pane was resized. The manager substitutes the last known inset
  /// instead.
  int contentInset = -1;
};

/**
 * @brief Manages the virtual container widget lifecycle and positioning.
 *
 * Handles creation, cleanup, and positioning of the virtual scrolling
 * container. Delegates to SelectionOverlayManager for overlay parenting.
 */
class VirtualContainerManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(VirtualContainerManager)

  // Test access (2026-08-18): container placement is pure arithmetic over
  // explicit arguments, but it is private because nothing outside the
  // class should be positioning the container. The alignment contract is
  // worth pinning directly rather than through a populated grid.
  friend class TestVirtualContainerManager;

public:
  explicit VirtualContainerManager(QObject *parent = nullptr);
  ~VirtualContainerManager() override;

  // Setup
  void setGridContainer(QWidget *container) { m_gridContainer = container; }
  void setScrollArea(QScrollArea *scrollArea) { m_scrollArea = scrollArea; }
  // Defined out-of-line so QPointer<>::operator= can static_cast through
  // the complete type rather than the fwd-declared one (Kartend-kovt).
  void setOverlayManager(SelectionOverlayManager *overlay);
  void setFilterManager(FilterManager *filter);

  // Container lifecycle
  void createContainer();
  void cleanupContainer();

  // Accessors
  [[nodiscard]] QWidget *container() const { return m_virtualContainer; }
  [[nodiscard]] bool hasContainer() const { return m_virtualContainer; }

  // Scroll area queries
  [[nodiscard]] int getEffectiveViewportWidth() const;
  [[nodiscard]] int getScrollbarWidth() const;
  [[nodiscard]] bool willNeedVerticalScrollbar(int totalHeight) const;

  // Container positioning
  void positionContainer(const ContainerPositionParams &params);

private:
  // Positioning helpers
  void setupContainerSizes(int availableWidth, int contentWidth, int totalHeight, bool overflow);
  [[nodiscard]] HorizontalAlignment
  getEffectiveAlignment(const ContainerPositionParams &params) const;
  [[nodiscard]] int calculateContainerPosition(int availableWidth, int contentWidth,
                                               HorizontalAlignment align,
                                               int contentInset = 0) const;
  /// Resolves ContainerPositionParams::contentInset against the last known
  /// one. A NEGATIVE report means "no cell was materialized to measure this
  /// pass", which is not the same as an inset of zero: the pool empties for a
  /// beat during any relayout, so a details-pane resize reported 47, 0, 47 and
  /// the two apparent changes threw away the held position — the grid kept
  /// jumping even after it was supposed to hold (field report 2026-08-20).
  [[nodiscard]] static int resolveContentInset(int reported, int lastKnown) {
    return reported >= 0 ? reported : qMax(0, lastKnown);
  }
  void configureHorizontalScrollbar(bool overflow);

  QWidget *m_gridContainer = nullptr;
  QScrollArea *m_scrollArea = nullptr;
  QWidget *m_virtualContainer = nullptr;
  /// Last MEASURED cell inset. Only kept so a pass with nothing materialized
  /// can reuse it (see resolveContentInset) — the POSITION itself is
  /// recomputed from scratch every time.
  int m_lastContentInset = -1;
  // Borrowed from ScrollManager's owned sub-objects. QPointer guards
  // against dangling reads if a future refactor changes the destruction
  // order of these siblings relative to VirtualContainerManager
  // (Kartend-kovt). Implicit T*-conversion keeps existing call sites
  // unchanged.
  QPointer<SelectionOverlayManager> m_overlayManager;
  QPointer<FilterManager> m_filterManager;
};

#endif // VIRTUALCONTAINERMANAGER_H
