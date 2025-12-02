#ifndef VIRTUALCONTAINERMANAGER_H
#define VIRTUALCONTAINERMANAGER_H

#include "collectionutils.h"
#include <QObject>

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
};

/**
 * @brief Manages the virtual container widget lifecycle and positioning.
 * 
 * Handles creation, cleanup, and positioning of the virtual scrolling container.
 * Delegates to SelectionOverlayManager for overlay parenting.
 */
class VirtualContainerManager : public QObject {
  Q_OBJECT
public:
  explicit VirtualContainerManager(QObject *parent = nullptr);
  ~VirtualContainerManager() override;

  // Setup
  void setGridContainer(QWidget *container) { m_gridContainer = container; }
  void setScrollArea(QScrollArea *scrollArea) { m_scrollArea = scrollArea; }
  void setOverlayManager(SelectionOverlayManager *overlay) { m_overlayManager = overlay; }
  void setFilterManager(FilterManager *filter) { m_filterManager = filter; }

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
  void setupContainerSizes(int availableWidth, int contentWidth, 
                          int totalHeight, bool overflow);
  [[nodiscard]] HorizontalAlignment getEffectiveAlignment(
      const ContainerPositionParams &params) const;
  void calculateScrollbarOffsets(bool verticalBarHidden, int &leftOffset,
                                int &rightOffset, int &centerOffset) const;
  [[nodiscard]] int calculateContainerPosition(int availableWidth, int contentWidth,
                                bool overflow, HorizontalAlignment align,
                                int leftOffset, int rightOffset,
                                int centerOffset) const;
  void configureHorizontalScrollbar(bool overflow);

  QWidget *m_gridContainer = nullptr;
  QScrollArea *m_scrollArea = nullptr;
  QWidget *m_virtualContainer = nullptr;
  SelectionOverlayManager *m_overlayManager = nullptr;
  FilterManager *m_filterManager = nullptr;
};

#endif // VIRTUALCONTAINERMANAGER_H
