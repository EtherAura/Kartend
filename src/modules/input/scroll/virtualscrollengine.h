#ifndef VIRTUALSCROLLENGINE_H
#define VIRTUALSCROLLENGINE_H

#include "collectionutils.h"

#include <limits>

#include <QObject>
#include <QString>

class ScrollManager;

/**
 * @brief Virtual scrolling engine extracted from ScrollManager.
 *
 * Owns the algorithms that drive virtual-scrolling layout, container
 * lifecycle, and widget materialization. Originally these methods lived as
 * sibling-TU members of ScrollManager (scrollmanagervirtual.cpp). Promoting
 * them into a real class reduces ScrollManager's surface area without
 * disturbing the canonical state — which still lives on ScrollManager and is
 * accessed here via friendship.
 *
 * Lifetime: owned by ScrollManager via std::unique_ptr; destroyed before any
 * borrowed state.
 */
class VirtualScrollEngine : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(VirtualScrollEngine)
public:
  explicit VirtualScrollEngine(ScrollManager *owner);
  ~VirtualScrollEngine() override = default;

  // Layout / metrics
  void calculateVirtualMetrics();
  void positionVirtualContainer();
  void enforceScrollContentConstraints();
  void recalculateContainerMetrics();
  void forceVirtualViewUpdate();
  void preCalculateLayout();
  void recreateLayout();
  void handleLayoutChange();
  void centerHorizontalScrollbar();
  void primeLayoutFor(const CollectionConfig &config);

  // Container lifecycle
  void createVirtualContainer();
  void cleanupVirtualContainer();

  // Scroll event wiring
  void connectScrollEvents();
  void disconnectScrollEvents();

  // Widget materialization
  void updateVirtualView();
  void ensureWidgetForIndex(int visualIndex);
  void reconfigureArtworkForActiveWidgets();

  // Path resolution helpers
  [[nodiscard]] QString virtualFolderPathForVisualIndex(int visualIndex) const;
  [[nodiscard]] QString filePathForVisualIndex(int visualIndex) const;

private:
  // Internal helpers (formerly private members of ScrollManager).
  // Kartend-gro2: needed indices are a contiguous half-open range, so a struct
  // beats QSet<int>'s hash-table allocation on the per-animation-frame hot path.
  struct NeededRange {
    int firstIndex = 0;
    int lastIndex = -1; // inclusive; lastIndex < firstIndex means empty
    [[nodiscard]] bool isEmpty() const { return lastIndex < firstIndex; }
    [[nodiscard]] int size() const { return isEmpty() ? 0 : (lastIndex - firstIndex + 1); }
    [[nodiscard]] bool contains(int idx) const {
      return idx >= firstIndex && idx <= lastIndex;
    }
  };
  [[nodiscard]] NeededRange calculateNeededIndices() const;
  void removeUnneededWidgets(const NeededRange &needed);
  void updateArtworkIfAllowed();

  ScrollManager *m_owner = nullptr; // back-pointer; not owned

  // Tracks the committedSelectedIndex value at the last overlay raise() call
  // so updateVirtualView() can skip the raise on smooth-scroll animation
  // frames where the selection hasn't changed (Kartend-eukc). QWidget::raise
  // performs a full re-stacking + paint on the overlay and its siblings;
  // skipping it ~60×/s during animation eliminates a hot path. Initialized
  // to INT_MIN so the first call after construction always raises.
  int m_lastRaisedSelectionIndex = std::numeric_limits<int>::min();
};

#endif // VIRTUALSCROLLENGINE_H
