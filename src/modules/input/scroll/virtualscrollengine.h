#ifndef VIRTUALSCROLLENGINE_H
#define VIRTUALSCROLLENGINE_H

#include "collectionutils.h"

#include <QObject>
#include <QSet>
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
  [[nodiscard]] QSet<int> calculateNeededIndices() const;
  void removeUnneededWidgets(const QSet<int> &needed);
  void updateArtworkIfAllowed();

  ScrollManager *m_owner = nullptr; // back-pointer; not owned
};

#endif // VIRTUALSCROLLENGINE_H
