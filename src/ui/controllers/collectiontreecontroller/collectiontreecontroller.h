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
class QToolButton;
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
  /// Kartend-auh7u: the outermost sidebar row (MainWindow's
  /// m_sidebarRowLayout) for FullHeight-justified docks, plus the toolbar
  /// column widget inside it. The tree claims the row's EXTREMES
  /// (insertWidget(0)/addWidget) so it always sits outside a full-height
  /// details pane, which inserts adjacent to the column. Optional —
  /// without them, FullHeight degrades to the below-toolbar dock.
  QHBoxLayout *fullHeightLayout = nullptr;
  QWidget *toolbarColumnWidget = nullptr;
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

protected:
  /// Drives the width-grip drag (press/move/release on m_grip). The grip is a
  /// plain child widget rather than a QSplitter because the panel docks by
  /// plain layout insertion into layouts the controller does not own —
  /// re-parenting the whole content row into a splitter would entangle this
  /// panel with the details pane's and toolbar's insertion logic
  /// (Kartend-auh7u) for one draggable edge.
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void insertPanelAt(DetailsPanePosition position, SidebarJustification justification);
  /// Places m_grip on the panel's inner edge for @p position and applies
  /// @p width (clamped) to the panel. Called from applyStateForCollection and
  /// the drag handler.
  void applyPanelWidth(int width, DetailsPanePosition position);
  /// Re-styles every existing row IN PLACE — icon (depth-aware width cap,
  /// style/tint/silhouette-source selection) and icons-only text handling —
  /// without clearing the tree. This is what display-option changes, panel
  /// resizes, and accent re-themes call: a full rebuildTree() on collection
  /// SWITCH visibly flashed and reset scroll (field report 2026-08-17),
  /// because the icon prefs are per-collection. rebuildTree() itself ends by
  /// calling this, so build-time and refresh-time styling cannot drift.
  void refreshIcons();
  /// Shows the slim edge marker exactly when the panel is hidden (user
  /// request 2026-08-17: a hidden tree needs a visible way back). Call after
  /// anything that changes the panel's visibility.
  void syncFoldMarker();
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
  QHBoxLayout *m_fullHeightLayout = nullptr;
  QWidget *m_toolbarColumnWidget = nullptr;
  std::function<void()> m_persistCollections;

  QWidget *m_panel = nullptr;
  QTreeWidget *m_tree = nullptr;
  /// Drag-to-resize grip on the panel's INNER edge (the side facing the
  /// content view — right edge when docked Left, left edge when Right).
  /// Width lives on cfg.collectionTree.treeWidth per collection; the drag
  /// resizes live and persists on release (user request 2026-08-17).
  QWidget *m_grip = nullptr;
  /// Slim clickable strip shown at the panel's dock edge while the panel is
  /// HIDDEN — the visible "unfold" affordance. Clicking toggles the tree
  /// back on (same path as F6, so per-collection visibility persists).
  QToolButton *m_foldMarker = nullptr;
  int m_dragStartX = 0;
  int m_dragStartWidth = 0;
  /// Active collection's icon display options (user request 2026-08-17),
  /// cached so applyStateForCollection can detect a change and rebuild —
  /// rebuildTree() bakes them into the rows.
  bool m_iconsOnly = false;
  int m_iconSize = 16;
  TreeIconStyle m_iconStyle = TreeIconStyle::Normal;
  QString m_iconTint;
  /// Panel width the current icon pixmaps were baked against (stamped by
  /// refreshIcons). Per-collection treeWidth means a collection switch can
  /// resize the panel; a mismatch here triggers a rebake so wide logos never
  /// clip at the new edge.
  int m_bakedPanelWidth = 0;
  /// Last accent colour applyPrimaryColor delivered — the Tinted style's
  /// default ink when no explicit tint colour is configured.
  QString m_accentColor;

  /// Where the panel is currently inserted, to avoid churning the layout on
  /// every switch that keeps the side.
  DetailsPanePosition m_insertedPosition = DetailsPanePosition::Left;
  SidebarJustification m_insertedJustification = SidebarJustification::BelowToolbar;
  bool m_panelInserted = false;
  /// Collapse memory across rebuilds, keyed by collection UUID (stable
  /// across index shuffles; the synthetic Playlists group uses a reserved
  /// key). INVERTED per user decision 2026-08-17: every branch defaults
  /// EXPANDED and only deliberate collapses are remembered, so new
  /// collections and fresh sessions open fully unfolded.
  QSet<QString> m_collapsedUuids;
  /// Guards the activation/expansion handlers while rebuilds and highlight
  /// moves mutate the tree programmatically.
  bool m_suppressSignals = false;
};

#endif // COLLECTIONTREECONTROLLER_H
