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

  /// Re-apply the last accent against the CURRENT desktop colours. The
  /// selection is tinted from the titlebar, which changes under a running
  /// app on a Plasma activity switch (user request 2026-08-19) — without
  /// this the sidebar keeps the colour it started with while the toolbar
  /// moves on, and the two visibly disagree.
  void refreshDesktopTint();

  [[nodiscard]] bool isPanelVisible() const;
  /// The tree widget, for focus checks (EventManager bypass) and tests.
  [[nodiscard]] QTreeWidget *treeWidget() const { return m_tree; }
  [[nodiscard]] QWidget *panel() const { return m_panel; }

signals:
  /// Mirrors the panel's effective visibility for menu-action sync.
  void visibilityChanged(bool visible);

protected:
  /// Drives the width drag: press/move/release inside a zone on the tree's
  /// INNER edge. A bare zone rather than a QSplitter because the panel docks
  /// by plain layout insertion into layouts the controller does not own —
  /// re-parenting the whole content row into a splitter would entangle this
  /// panel with the details pane's and toolbar's insertion logic
  /// (Kartend-auh7u) for one draggable edge. And a zone rather than a real
  /// grip WIDGET because any widget paints something between the sidebar and
  /// the toolbar, which read as a gap (field report 2026-08-18).
  ///
  /// The events arrive on the tree's VIEWPORT, not the tree — that is where
  /// QTreeWidget sends mouse input — so the viewport branch above must not
  /// swallow anything but Resize.
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  /// Places the panel for @p mode. Expand docks it into the row (it takes
  /// layout width); Overlay floats it above the content so the items
  /// viewport keeps its full geometry and the grid underneath does not move.
  void insertPanelAt(DetailsPanePosition position, SidebarJustification justification,
                     DetailsPaneMode mode);
  /// Re-anchors the floating panel to its host's current geometry. No-op
  /// unless the panel is in Overlay mode.
  void positionOverlayPanel();
  /// Applies @p width (clamped to [kMinWidth, kMaxWidth]) to the panel.
  /// Called from applyStateForCollection and the drag handler. @p position is
  /// unused now that the drag zone lives on the tree's inner edge rather than
  /// on a widget that had to be re-seated per dock side.
  void applyPanelWidth(int width, DetailsPanePosition position);
  /// Re-styles every existing row IN PLACE — icon (depth-aware width cap,
  /// style/tint/silhouette-source selection) and icons-only text handling —
  /// without clearing the tree. This is what display-option changes, panel
  /// resizes, and accent re-themes call: a full rebuildTree() on collection
  /// SWITCH visibly flashed and reset scroll (field report 2026-08-17),
  /// because the icon prefs are per-collection. rebuildTree() itself ends by
  /// calling this, so build-time and refresh-time styling cannot drift.
  void refreshIcons();
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
  /// The widget the panel FLOATS over in Overlay mode (the owner of whichever
  /// layout it would otherwise have docked into). Null in Expand mode. Its
  /// resizes are watched so the floating panel keeps spanning it.
  QWidget *m_overlayHost = nullptr;
  DetailsPaneMode m_insertedMode = DetailsPaneMode::Expand;
  QWidget *m_toolbarColumnWidget = nullptr;
  std::function<void()> m_persistCollections;

  QWidget *m_panel = nullptr;
  QTreeWidget *m_tree = nullptr;
  /// The root row pinned to the top of the tree while it scrolls. Typed as
  /// QWidget because the concrete class is file-local to the .cpp.
  QWidget *m_stickyRootHeader = nullptr;
  /// True between press and release inside the tree's inner-edge drag
  /// zone. Without it a plain click on a row would be read as a resize.
  bool m_resizingPanel = false;
  int m_dragStartX = 0;
  int m_dragStartWidth = 0;
  /// Active collection's icon display options (user request 2026-08-17),
  /// cached so applyStateForCollection can detect a change and rebuild —
  /// rebuildTree() bakes them into the rows.
  bool m_iconsOnly = false;
  int m_iconSize = 16;
  TreeIconStyle m_iconStyle = TreeIconStyle::Normal;
  QString m_iconTint;
  /// Render the ACTIVE collection's logo in colour even under a
  /// monochrome/tinted style (user request 2026-08-18).
  bool m_colorizeSelected = false;
  /// Panel width the current icon pixmaps were baked against (stamped by
  /// refreshIcons). Per-collection treeWidth means a collection switch can
  /// resize the panel; a mismatch here triggers a rebake so wide logos never
  /// clip at the new edge.
  int m_bakedPanelWidth = 0;
  /// Viewport width the icons were baked against. The scrollbar appearing or
  /// vanishing resizes the VIEWPORT without touching the panel width, and a
  /// stale bake clips centered logos at the new edge (field report
  /// 2026-08-17) — a viewport resize triggers a rebake via eventFilter.
  int m_bakedViewportWidth = 0;
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
