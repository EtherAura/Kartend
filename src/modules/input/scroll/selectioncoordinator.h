#ifndef SELECTIONCOORDINATOR_H
#define SELECTIONCOORDINATOR_H

#include <QObject>
#include <QPoint>
#include <QRect>

class QWidget;
class ItemWidget;
class SelectionOverlayManager;

/**
 * @brief Coordinates selection state and overlay animations.
 *
 * Encapsulates the logic for tracking selected indices, calculating movement
 * direction, and coordinating with SelectionOverlayManager for visual updates.
 *
 * This is an incremental extraction from ScrollManager - reduces cognitive
 * complexity of selection handling by grouping related state and operations.
 */
class SelectionCoordinator : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SelectionCoordinator)
public:
  explicit SelectionCoordinator(QObject *parent = nullptr);
  ~SelectionCoordinator() override = default;

  // Setup
  void setOverlayManager(SelectionOverlayManager *overlay);
  void setGridContainer(QWidget *container) { m_gridContainer = container; }

  // Selection state
  [[nodiscard]] int selectedIndex() const { return m_selectedIndex; }
  [[nodiscard]] int committedIndex() const { return m_committedIndex; }
  [[nodiscard]] int selectedRow() const { return m_selectedRow; }
  [[nodiscard]] int selectionDirection() const { return m_direction; }

  void setSelectedIndex(int index);
  void setCommittedIndex(int index) { m_committedIndex = index; }
  void setSelectedRow(int row) { m_selectedRow = row; }

  // Movement analysis
  struct MovementInfo {
    bool isHorizontal = false;
    int direction = 0; // -1, 0, or 1
  };
  [[nodiscard]] MovementInfo analyzeMovement(int newIndex, int prevIndex, int itemsPerRow) const;

  // Overlay rect calculation (requires item position callback)
  using PositionCallback = std::function<QPoint(int)>;
  using MetricsCallback = std::function<std::pair<int, int>()>; // returns (width, height)

  void setPositionCallback(PositionCallback cb) { m_getPosition = std::move(cb); }
  void setMetricsCallback(MetricsCallback cb) { m_getMetrics = std::move(cb); }

  [[nodiscard]] QRect rectForIndex(int visualIndex, int totalItems) const;

  // Reset state
  void reset();

signals:
  void selectionChanged(int newIndex, int prevIndex);

private:
  SelectionOverlayManager *m_overlayManager = nullptr;
  QWidget *m_gridContainer = nullptr;

  int m_selectedIndex = -1;
  int m_committedIndex = -1;
  int m_selectedRow = -1;
  int m_direction = 0;

  PositionCallback m_getPosition;
  MetricsCallback m_getMetrics;
};

#endif // SELECTIONCOORDINATOR_H
