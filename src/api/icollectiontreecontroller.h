#ifndef ICOLLECTIONTREECONTROLLER_H
#define ICOLLECTIONTREECONTROLLER_H

/// Role interface for the collection tree panel (Kartend-ob1c9) — the
/// hidable, left/right dockable tree navigator. Deliberately tiny: the input
/// layer (keyboard binding, gamepad action) only ever needs the toggle;
/// everything else about the panel is wired directly by MainWindow, which
/// owns the concrete CollectionTreeController.
class ICollectionTreeController {
public:
  virtual ~ICollectionTreeController() = default;

  /// Toggle the panel for the active collection (persisted per collection;
  /// a root-view toggle is live-only).
  virtual void toggleVisible() = 0;
};

#endif // ICOLLECTIONTREECONTROLLER_H
