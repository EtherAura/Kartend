#ifndef COLLECTIONTREECONTROLLER_H
#define COLLECTIONTREECONTROLLER_H

#include <functional>

#include <QObject>
#include <QSet>
#include <QString>

#include "applicationcontext.h"
#include "collection/collectiontreesettings.h"
#include "icollectiontreecontroller.h"

class QHBoxLayout;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

/// Kartend-ob1c9: the collection tree panel — a hidable, left/right dockable
/// tree navigator over collections (with a grouped Playlists section) in the
/// main window. NOT the "sidebar": that name belongs to the details pane
/// (SidebarAppearance, DetailsPaneManager) everywhere in this codebase.
///
/// Follows the DetailsPaneManager shape: ctx + setup struct (no
/// sibling-manager pointers — navigation is reached through
/// ctx->navigationManager() at point of use), docks by layout insertion
/// into the items page's horizontal layout, and per-collection state lives
/// on CollectionConfig (cfg.collectionTree) so visibility and side follow
/// the active collection.
struct CollectionTreeControllerSetup {
  const ApplicationContext *ctx = nullptr;

  /// The items page's horizontal content layout (m_mainHorizontalLayout).
  /// Left docks insertWidget(0), Right docks addWidget — the same slots the
  /// details pane uses in Fixed mode; whichever inserts later sits further
  /// out on a shared side (v1 accepts that; the tree defaults Left, the
  /// pane defaults Right, so they only contend when reconfigured to the
  /// same side).
  QHBoxLayout *mainLayout = nullptr;
  /// Parent for the panel widget (the main content widget that owns
  /// mainLayout).
  QWidget *panelParent = nullptr;
  /// Owner-supplied debounced collections persist
  /// (MainWindow::requestDebouncedCollectionsSave). Null in headless
  /// contexts; call sites guard.
  std::function<void()> persistCollections;
};

// QObject must be the first base; ICollectionTreeController is a plain
// (non-QObject) role interface — single-QObject-base multiple inheritance,
// the DetailsPaneManager pattern.
class CollectionTreeController : public QObject, public ICollectionTreeController {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CollectionTreeController)

public:
  explicit CollectionTreeController(QObject *parent = nullptr);
  ~CollectionTreeController() override;

  void setupReferences(const CollectionTreeControllerSetup &setup);
  /// Builds the panel widget, inserts it into the layout at the current
  /// collection's configured side, populates the tree, and applies the
  /// current visibility. Requires setupReferences() first.
  void setupPanel();

  /// Rebuild rows from the current collections + hierarchy cache. Expansion
  /// is remembered across rebuilds by collection UUID (in-memory; session
  /// persistence is a follow-up). Call whenever the collection list changes
  /// (MainWindow::rebuildHierarchyCache is the chokepoint).
  void rebuildTree();

  /// Apply per-collection state for the newly active collection: visibility
  /// + dock side from cfg.collectionTree, and move the selection highlight
  /// to the first row showing @p collectionIndex. -1 (root view / cleared)
  /// keeps the panel as-is and clears the highlight — the panel stays
  /// useful as the navigator on the home view.
  void onCollectionSwitched(int collectionIndex);

  /// Toggle the panel for the ACTIVE collection and persist. On the root
  /// view (no active collection) the widget still toggles so the shortcut
  /// never feels dead, but nothing is persisted — there is no collection to
  /// remember it on.
  void toggleVisible() override;

  /// Reassign the dock side for the ACTIVE collection and persist.
  /// Only Left and Right are accepted; anything else is ignored.
  void setDockPosition(DetailsPanePosition position);

  /// The active collection's configured dock side (falls back to the
  /// panel's current side on the root view). Drives the position submenu's
  /// check state.
  [[nodiscard]] DetailsPanePosition activeDockPosition() const;

  /// Per-collection theming hook — called alongside
  /// CollectionBackgroundController::applyPrimaryColorForCollection so the
  /// panel re-themes with the rest of the chrome. Empty resets to palette
  /// defaults.
  void applyPrimaryColor(const QString &hexColor);

  [[nodiscard]] bool isPanelVisible() const;
  /// The tree widget, for focus checks (EventManager bypass) and tests.
  [[nodiscard]] QTreeWidget *treeWidget() const { return m_tree; }
  [[nodiscard]] QWidget *panel() const { return m_panel; }

signals:
  /// Mirrors the panel's effective visibility for menu-action sync.
  void visibilityChanged(bool visible);

private:
  void insertPanelAt(DetailsPanePosition position);
  void applyStateForCollection(int collectionIndex);
  void highlightCollection(int collectionIndex);
  void onItemActivated(QTreeWidgetItem *item);
  void onItemExpandedCollapsed(QTreeWidgetItem *item, bool expanded);
  [[nodiscard]] int activeCollectionIndex() const;
  [[nodiscard]] const CollectionConfig *activeCollection() const;
  [[nodiscard]] CollectionConfig *activeCollectionMutable();

  const ApplicationContext *m_ctx = nullptr;
  QHBoxLayout *m_mainLayout = nullptr;
  QWidget *m_panelParent = nullptr;
  std::function<void()> m_persistCollections;

  QWidget *m_panel = nullptr;
  QLabel *m_header = nullptr;
  QTreeWidget *m_tree = nullptr;

  /// Where the panel is currently inserted, to avoid churning the layout on
  /// every switch that keeps the side.
  DetailsPanePosition m_insertedPosition = DetailsPanePosition::Left;
  bool m_panelInserted = false;
  /// Expansion memory across rebuilds, keyed by collection UUID (stable
  /// across index shuffles). The synthetic Playlists group uses a reserved
  /// key.
  QSet<QString> m_expandedUuids;
  /// Guards the activation/expansion handlers while rebuilds and highlight
  /// moves mutate the tree programmatically.
  bool m_suppressSignals = false;
};

#endif // COLLECTIONTREECONTROLLER_H
