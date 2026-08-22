#ifndef VIRTUALSCROLLENGINE_H
#define VIRTUALSCROLLENGINE_H

#include "collection/collectionconfig.h"

#include <limits>
#include <optional>

#include <QObject>
#include <QPointer>
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
    [[nodiscard]] bool contains(int idx) const { return idx >= firstIndex && idx <= lastIndex; }
  };
  [[nodiscard]] NeededRange calculateNeededIndices() const;
  void removeUnneededWidgets(const NeededRange &needed);
  void updateArtworkIfAllowed();

  // Snapshot of the per-widget visual config (everything ensureWidgetForIndex
  // pushes onto a materialized widget except geometry). During smooth scroll
  // only positions change, so re-pushing title/font/corner/dimension setters to
  // every visible widget ~60×/s is wasted work (Kartend-8ucd). updateVirtualView
  // computes the current signature once per pass and only re-applies the setters
  // when it differs from the last applied one; geometry is always set.
  struct WidgetVisualConfig {
    bool hideTitles = false;
    bool hideSubcollectionTitles = false;
    int fontSize = -1;
    int cornerRadius = -1;
    int itemWidth = -1;
    int itemHeight = -1;
    bool operator==(const WidgetVisualConfig &) const = default;
  };
  [[nodiscard]] WidgetVisualConfig currentWidgetVisualConfig() const;

  // Back-pointer to the owning ScrollManager. QPointer guards against
  // dangling reads if a future refactor changes the destruction order
  // — VirtualScrollEngine is Qt-parented under ScrollManager so under
  // normal teardown m_owner is guaranteed live for the engine's whole
  // lifetime (Kartend-kovt).
  QPointer<ScrollManager> m_owner;

  // Tracks the committedSelectedIndex value at the last overlay raise() call
  // so updateVirtualView() can skip the raise on smooth-scroll animation
  // frames where the selection hasn't changed (Kartend-eukc). QWidget::raise
  // performs a full re-stacking + paint on the overlay and its siblings;
  // skipping it ~60×/s during animation eliminates a hot path. Initialized
  // to INT_MIN so the first call after construction always raises.
  int m_lastRaisedSelectionIndex = std::numeric_limits<int>::min();

  // Last visual config applied to materialized widgets, and whether the current
  // updateVirtualView pass saw a change (drives the skip in ensureWidgetForIndex).
  // Defaults differ from any real config so the first pass always re-applies.
  WidgetVisualConfig m_lastWidgetVisualConfig;
  bool m_widgetVisualConfigDirty = true;

  // View type this engine last laid out with (Kartend-8pxzi). handleLayoutChange
  // needs to tell a view-type SWITCH, which genuinely needs fresh widgets (list
  // mode builds no image label), from a geometry-only relayout such as a
  // details-pane resize, which does not. The config is already updated by the
  // time handleLayoutChange runs, so the previous value cannot be recovered
  // from it and has to be remembered here. Empty until the first layout, so
  // that first pass always counts as structural.
  std::optional<ViewType> m_lastLaidOutViewType;
};

#endif // VIRTUALSCROLLENGINE_H
