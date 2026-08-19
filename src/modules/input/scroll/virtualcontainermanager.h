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
                                               HorizontalAlignment align) const;
  void configureHorizontalScrollbar(bool overflow);

  QWidget *m_gridContainer = nullptr;
  QScrollArea *m_scrollArea = nullptr;
  QWidget *m_virtualContainer = nullptr;
  // Borrowed from ScrollManager's owned sub-objects. QPointer guards
  // against dangling reads if a future refactor changes the destruction
  // order of these siblings relative to VirtualContainerManager
  // (Kartend-kovt). Implicit T*-conversion keeps existing call sites
  // unchanged.
  QPointer<SelectionOverlayManager> m_overlayManager;
  QPointer<FilterManager> m_filterManager;
};

#endif // VIRTUALCONTAINERMANAGER_H
