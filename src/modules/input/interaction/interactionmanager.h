#ifndef INTERACTIONMANAGER_H
#define INTERACTIONMANAGER_H

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/collectionhierarchycache.h"
#include "collection/generalsettings.h"
#include "dialogrunners.h"
#include "iinteractionmanager.h"
#include "interactionstateholder.h" // Required: m_state is a value member
#include "itemmetadata.h"
#include "launchpreview.h"          // LaunchPreview leaf header (Kartend-rq33v)
#include "playlistmenucontroller.h" // PlaylistMenuController + SmartPlaylist* typedefs
#include "setuputils.h"
#include "smartfilter.h"
#include <functional>
#include <memory>
#include <optional>
#include <QObject>
#include <QPointer>
#include <QScrollArea>
// Full Qt widget headers (rather than fwd-decls) because the matching members
// below use QPointer<T> which needs the complete type for the static_cast
// inside the assignment / conversion operators (Kartend-ccl0).
#include <QLineEdit>
#include <QStackedWidget>

QT_BEGIN_NAMESPACE
class QAction;
class QKeyEvent;
class QPushButton;
class QTimer;
QT_END_NAMESPACE

// Forward declarations for owned sub-managers (only pointers used in header)
class AnimationManager;
class AttractManager;
class EventManager;
class GamepadManager;
class KeyboardManager;
class LaunchManager;
class MouseManager;
class SearchManager;
class SelectionManager;
class ViewportManager;
class ArrowNavigationHandler;
class AlphabeticNavigationHandler;
class ItemMetadataActionController;

class ItemWidget;
class IDatabaseManager;
class INavigationManager;
class ISettingsManager;
class IDetailsPaneManager;
class IDetailPageManager;
class IScrollManager;
class ISessionManager;
class IArtworkManager;
class IDetailsPane;

/**
 * @brief Setup struct for InteractionManager dependencies.
 *
 * Fields can be set individually, or common fields can be populated
 * from an ApplicationContext via the ctx pointer.
 */
// SmartPlaylistEdit / SmartPlaylistCollectionEntries / SmartPlaylistDialogRunner
// moved to playlistmenucontroller.h with the playlist actions (Kartend-5lmt7);
// re-exported via the include above so DialogController / MainWindow wiring
// keep compiling against this header.

/// Runs the modal EditMetadataDialog seeded with the item's title and the
/// current curation payload (notes, tags, rating, source URL, custom
/// fields). Returns the edited payload, or nullopt on cancel.
using EditMetadataDialogRunner = std::function<std::optional<EditMetadataPayload>(
    const QString &itemTitle, const EditMetadataPayload &initial)>;

/// Pops the launch-preview / dry-run dialog. Read-only, side-effect-free —
/// the caller resolves the LaunchPreview ahead of time and hands the
/// fully-baked struct in, so this closure just renders.
using LaunchPreviewDialogRunner =
    std::function<void(const QString &itemTitle, const QString &launcherName,
                       const QString &filePath, const LaunchPreview &preview)>;

struct InteractionManagerSetup {
  // ctx is the single source of truth for sibling managers. Per Kartend-mxn4
  // the per-manager pointer fields and their fallback getters were removed
  // — no setup-site populated them and InteractionManager already reaches
  // every sibling via m_ctx->xxxManager() (see scrollMgr() / databaseMgr() /
  // etc. accessors lower in this header).
  const ApplicationContext *ctx = nullptr;

  // UI elements (can be overridden or taken from ctx)
  IDetailsPane *sidebar = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *collectionPage = nullptr;
  QLineEdit *searchBar = nullptr;
  QAction *searchModeAction = nullptr;

  // State references (can be overridden or taken from ctx)
  QList<CollectionConfig> *collections = nullptr;
  const int *currentCollectionIndex = nullptr;
  GeneralSettings *generalSettings = nullptr;
  const bool *isShuttingDown = nullptr;
  const CollectionHierarchyCache *hierarchyCache = nullptr;

  /// Owner-supplied dialog runners (Kartend-n8kh). InteractionManager
  /// invokes these instead of constructing the dialog itself so the
  /// data->ui edge is gone — the UI layer provides the runner closure
  /// from MainWindow setup wiring.
  SmartPlaylistDialogRunner runSmartPlaylistDialog;
  EditMetadataDialogRunner runEditMetadataDialog;
  LaunchPreviewDialogRunner runLaunchPreviewDialog;
  /// Kartend-sqoq0: generic stock-Qt-modal runners (confirm / warn / info /
  /// getText / file pickers). Consumed here for the launcher-unavailable
  /// warning and the manual-file picker, and forwarded to
  /// PlaylistMenuController for the playlist menu's prompts. Null runners
  /// fall back to the original direct Qt dialog construction.
  DialogRunners dialogs;

  // UI element accessors that check ctx fallback
  SETUP_GETTER_INLINE_UI_SAME(QScrollArea *, ItemScrollArea, itemScrollArea)
  SETUP_GETTER_INLINE_UI_SAME(QWidget *, GridContainer, gridContainer)
  SETUP_GETTER_INLINE_UI_SAME(IDetailsPane *, Sidebar, sidebar)
  SETUP_GETTER_INLINE_UI_SAME(QStackedWidget *, StackedWidget, stackedWidget)
  SETUP_GETTER_INLINE_UI_SAME(QWidget *, ItemsPage, itemsPage)
  SETUP_GETTER_INLINE_UI_SAME(QLineEdit *, SearchBar, searchBar)
  SETUP_GETTER_INLINE_UI_SAME(QAction *, SearchModeAction, searchModeAction)
  SETUP_GETTER_INLINE_COL_SAME(QList<CollectionConfig> *, Collections, collections)
  SETUP_GETTER_INLINE_COL_SAME(const int *, CurrentCollectionIndex, currentCollectionIndex)
  SETUP_GETTER_INLINE_COL_SAME(const CollectionHierarchyCache *, HierarchyCache, hierarchyCache)
  SETUP_GETTER_INLINE_COL_SAME(GeneralSettings *, GeneralSettings, generalSettings)
  SETUP_GETTER_INLINE_COL_SAME(const bool *, IsShuttingDown, isShuttingDown)
};

/**
 * @brief Central coordinator for user input handling across keyboard, mouse,
 * and touch.
 *
 * Memory Ownership Model:
 * - Owns sub-managers via std::unique_ptr (explicit lifetime management):
 *   SearchManager, SelectionManager, KeyboardManager, ArrowNavigationHandler,
 *   AlphabeticNavigationHandler, AnimationManager, MouseManager, LaunchManager,
 *   ViewportManager, EventManager
 * - Owns InteractionStateHolder as value member (m_state)
 * - Does NOT own: m_scrollManager, m_detailsPaneManager, m_settingsManager,
 * m_databaseManager, m_navigationManager, m_sessionManager, m_artworkManager,
 * UI widgets (borrowed references)
 *
 * Sub-managers are registered in ApplicationContext after setup for sibling
 * access.
 */
class FocusSectionOverlay;
class SelectionIndicator;
class QAbstractScrollArea;

class InteractionManager : public QObject, public IInteractionManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(InteractionManager)
public:
  /// Select+direction section chord (user request 2026-08-17): move the
  /// keyboard focus between the app's sections — grid, toolbar row, left
  /// sidebar (collection tree), right sidebar (details pane) — honouring
  /// each section's visibility. dx/dy are -1/0/+1. Public for the
  /// integration tests.
  void moveFocusSection(int dx, int dy);
  /// Plain d-pad / left-stick ALWAYS control the grid (user decision
  /// 2026-08-17): if keyboard focus wandered to a sidebar or the toolbar
  /// (via the section chord), a plain direction press pulls it back to the
  /// grid before the move applies. Select+direction and the right stick
  /// remain the section/shortcut layer. Public for the integration tests.
  void returnGamepadFocusToGrid();
  /// Right-stick routing (user decision 2026-08-18): from the GRID the
  /// stick moves between sections; once a section owns focus the stick
  /// drives THAT section's list — tree rows and pane scroll on the
  /// vertical axis, toolbar buttons on the horizontal one, with the other
  /// axis left to section movement so there is always a way out. Up from
  /// the grid focuses the toolbar and down from the toolbar returns (user
  /// request 2026-08-24), unless the pane ring is mid-walk. Returns
  /// true when the input was consumed by a section. Public for the tests.
  bool routeSectionInput(int dx, int dy);
  /// The fullscreen artwork overlay, when one is up (either the grid's or
  /// the details pane's — both live under the window). Expand mode owns
  /// the gamepad while visible: Back dismisses it, directions cycle
  /// artwork, and nothing reaches the collection behind it (field report
  /// 2026-08-18). Public for the tests.
  /// NOT const: discovering the overlay is also where its boundary signal
  /// gets hooked. The hook used to live in the gamepad's key-sending
  /// helper, so keyboard and wheel cycling never reached the item-stepping
  /// logic at all (field report 2026-08-18).
  [[nodiscard]] QWidget *visibleArtworkOverlay();
  /// Called when an artwork overlay becomes visible so its boundary
  /// hand-off is connected REGARDLESS of which input opened it. Keyboard
  /// and wheel reach the overlay directly and never ask this class for
  /// anything, so without a deterministic hook their cycling stopped dead
  /// at the ends (field report 2026-08-18).
  void hookArtworkOverlayIfVisible() { (void)visibleArtworkOverlay(); }
  /// Feed @p key to the expanded overlay so its own handler runs — the
  /// same path the keyboard takes, so there is one policy, not two.
  bool sendKeyToArtworkOverlay(int key);
  /// While expand mode is up, step to the previous/next ITEM and show its
  /// artwork (user decision 2026-08-18, replacing the cycle-this-item's-
  /// artwork behaviour). Returns false when nothing is expanded.
  bool stepExpandedItem(int delta);
  /// Re-point the visible expand-mode overlay at the CURRENT selection.
  /// Split out because the selection settles asynchronously: stepping and
  /// re-showing in one breath read the pre-move index and left the old
  /// artwork on screen while the item moved underneath (field report
  /// 2026-08-18).
  void refreshExpandedArtwork();
  /// Slot for ArtworkPreviewOverlay::galleryBoundaryReached. A named
  /// member, not a lambda: Qt::UniqueConnection (which keeps the repeated
  /// hookup idempotent) only works with member-function pointers — with a
  /// functor it is a fatal error.
  void onExpandedGalleryBoundary(int direction);
  /// Repopulate the expanded overlay's thumb strip from the sidebar's
  /// gallery. Without it the strip kept the PREVIOUS item's artwork after
  /// a boundary step, so the overlay was still "at the end" and the next
  /// press stepped the item again instead of cycling the new one's art
  /// (field report 2026-08-18).
  void syncExpandedGalleryStrip();
  /// Path the expanded overlay is currently showing — lets the staged
  /// refreshes re-sync the strip without re-decoding the same image.
  QString m_expandedShownPath;
  /// Direction of the last boundary step, consumed once the new item's
  /// gallery arrives: stepping BACKWARD lands on the new item's LAST
  /// artwork, so pressing left again cycles that item instead of stepping
  /// straight past it (the mirror of the forward case).
  int m_expandedStepDirection = 0;
  /// Artwork paths the strip held when the boundary step was taken. The
  /// new item's gallery arrives asynchronously, so the backward landing
  /// must wait for the list to actually CHANGE — applying it against the
  /// stale list jumped to the old item's last artwork and consumed the
  /// pending step (field report 2026-08-18).
  QStringList m_expandedEntrySnapshot;
  /// Drive the details pane's own regions (artwork strip, description,
  /// metadata) with the right stick (user request 2026-08-18). Scrolls the
  /// SELECTED region; when it can go no further and @p allowAdvance is
  /// set, the selection steps to the neighbouring region instead — so a
  /// held deflection reads as one continuous travel down the pane. The
  /// pulsing indicator marks whatever is selected. Returns false when
  /// there is no pane to drive.
  bool driveDetailsPane(int steps, bool allowAdvance);
  /// Confirm (gamepad A) aimed at the focused section instead of the grid:
  /// activates the tree row or toolbar button under focus. Returns true
  /// when handled, so the caller does NOT also launch the grid selection —
  /// without this, pressing A while browsing the tree launched whatever
  /// the grid happened to have selected.
  bool activateFocusedSection();
  /// Show/hide the modifier HUD (desaturated backdrop + section indicator)
  /// while the gamepad's Select modifier is held (user request
  /// 2026-08-18). Public for the integration tests.
  void setFocusModifierActive(bool active);
  /// Scroll the details pane by @p steps notches when it is visible and
  /// scrollable; returns false when the pane could not take the scroll, so
  /// the caller can fall back to section movement (user request
  /// 2026-08-18: the right stick scrolls the description).
  bool scrollDetailsPane(int steps);
  explicit InteractionManager(QObject *parent = nullptr);
  ~InteractionManager() override;
  void setupReferences(const InteractionManagerSetup &setup);

  // ─────────────────────────────────────────────────────────────────────────
  // Owned sub-manager accessors - allows ApplicationContext to register them
  // ─────────────────────────────────────────────────────────────────────────
  [[nodiscard]] AnimationManager *animationManager() const { return m_animationManager.get(); }
  [[nodiscard]] AttractManager *attractManager() const override { return m_attractManager.get(); }
  [[nodiscard]] SelectionManager *selectionManager() const { return m_selectionManager.get(); }
  [[nodiscard]] ViewportManager *viewportManager() const { return m_viewportManager.get(); }
  [[nodiscard]] MouseManager *mouseManager() const { return m_mouseManager.get(); }
  [[nodiscard]] KeyboardManager *keyboardManager() const { return m_keyboardManager.get(); }
  [[nodiscard]] GamepadManager *gamepadManager() const override { return m_gamepadManager.get(); }
  [[nodiscard]] EventManager *eventManager() const { return m_eventManager.get(); }
  [[nodiscard]] SearchManager *searchManager() const { return m_searchManager.get(); }
  [[nodiscard]] LaunchManager *launchManager() const { return m_launchManager.get(); }
  // Extracted action controllers (Kartend-5lmt7 / Kartend-i5ai0). Callers
  // reach the item-metadata mutations and playlist menu actions through
  // these accessors directly — the facade no longer keeps one-line
  // delegating wrappers for them.
  [[nodiscard]] ItemMetadataActionController *itemMetadataActions() const {
    return m_itemMetadataActions.get();
  }
  [[nodiscard]] PlaylistMenuController *playlistMenu() const { return m_playlistMenu.get(); }

  // ─────────────────────────────────────────────────────────────────────────
  // Public API
  // ─────────────────────────────────────────────────────────────────────────

  // Widget double-click handling (used by EventManager)
  void handleWidgetDoubleClickedWithCollection(const QString &filePath, int collectionIndex);
  void selectItemByIndex(int index, bool allowHorizontalScroll);
  void clearSelection();
  void clearSelectionAndFocus() override;
  [[nodiscard]] int currentSelectedIndex() const override;
  [[nodiscard]] int getCurrentGridWidth() const;
  void toggleSearchMode();
  void updateSearchModeButton();
  void updateSearchBarPlaceholder();
  void launchItemWithCollection(const QString &filePath, int collectionIndex);
  [[nodiscard]] bool isWheelScrolling() const override;
  auto eventFilter(QObject *obj, QEvent *event) -> bool override;
  auto handleGlobalKeyPress(QKeyEvent *event) -> bool;
  [[nodiscard]] QString selectedFilePath() const override;
  void ensureItemVisible(int index, bool allowHorizontalScroll);
  void applyImmediateViewportPositioningForSelection(int targetIndex);
  void initializeSearchModeForCurrentCollection() override;
  void beginSelectionRestore(int targetIndex) override;
  void cancelPendingSelectionRestore() override;
  void resetSelectionRestoreState() override; // Reset for new navigation (allows
                                              // auto-restore)
  void stopRepeat(bool suppressRecentering = false) override;
  // Stops any running scroll animations (wheel/arrow key) to prevent stale
  // animations from applying after a view rebuild (e.g., entering a subfolder).
  void stopScrollAnimations() override;
  // Shows a right-click context menu for the item at the given visual index.
  void showContextMenu(ItemWidget *widget, int visualIndex, const QPoint &globalPos);

  // Opens the read-only launch-preview / dry-run dialog. Resolves the
  // launcher (incl. preset + per-item override), builds the command via
  // LaunchManager::previewLaunchCommand, and hands the result to the
  // owner-supplied dialog runner. No external process is spawned.
  void previewLaunchCommand(const QString &filePath, const QString &itemName);

  // Item-metadata mutations (edit dialog, manual path, launcher override,
  // pin/hide/continue-later) live on ItemMetadataActionController; the
  // playlist context-menu actions (create/add/rename/delete, smart-playlist
  // dialogs, import/export) live on PlaylistMenuController. Reach both via
  // the itemMetadataActions() / playlistMenu() accessors above — the
  // delegating wrappers were deleted (Kartend-i5ai0).
  [[nodiscard]] bool isRestoringSelection() const;
  [[nodiscard]] int targetRestoreIndex() const;
  [[nodiscard]] bool forceImmediateCenter() const;
  void recenterCurrentSelection();

  // Navigation progress state - set by NavigationManager during collection
  // switches
  [[nodiscard]] bool isNavigationInProgress() const { return m_navigationInProgress; }
  void setNavigationInProgress(bool inProgress) override { m_navigationInProgress = inProgress; }

  // Centralized interaction state holder - provides typed access to state
  // that was previously stored as Qt dynamic properties
  [[nodiscard]] InteractionStateHolder &state() { return m_state; }
  [[nodiscard]] const InteractionStateHolder &state() const { return m_state; }

private:
  bool m_navigationInProgress = false;
  // Set true at the top of the destructor so slots can short-circuit when
  // late signals fire during sub-manager teardown.
  bool m_destroying = false;
  InteractionStateHolder m_state;

  // Selection restore tokens (restoreToken / restorePending) live in
  // m_state.selectionRestore() — the canonical owner. NavigationManager and
  // SelectionRestoreCoordinator coordinate through the same struct.

signals:
  void selectionChanged(int index);

public slots:
  void saveCurrentSelection() override;
  void handleImmediateSearchTextChanged(const QString &text);
  /// Triggered when the user activates (Enter / double-click) while the
  /// artwork preview overlay is visible. Hides the overlay, clears expand
  /// state, and launches the previewed item (falling back to the current
  /// selection when @p filePath is empty, e.g. for sidebar gallery
  /// previews that don't carry a media path).
  void onArtworkPreviewLaunchRequested(const QString &filePath = QString());
  /// Triggered when the user middle-clicks a grid/list item.
  /// Opens a video-first preview overlay for the clicked item without
  /// changing selection or starting the expand-mode launch sequence.
  void onMediaPreviewRequested(ItemWidget *widget, int visualIndex);
  /// Triggered when the user middle-clicks with the artwork-cycle modifier
  /// held. Resolves the item's path and owning collection
  /// then asks ArtworkManager to advance to the next available artwork
  /// type for that item. No selection or launch side-effects.
  void onArtworkTypeCycleRequested(ItemWidget *widget, int visualIndex);

private slots:
  // KeyboardManager callbacks
  void handleArrowKeyNavigation(int direction, bool vertical);
  void handleAlphabeticNavigation(bool forward);
  void handleJumpToEdge(bool toEnd);
  void onKeyboardRepeatStep();
  void onKeyboardStopRepeat(bool suppressRecentering);

  // MouseManager callbacks
  void onMouseHoldScrollStep(int direction, bool isHorizontal);

private:
  [[nodiscard]] QList<int> getSubcollections(int parentIndex) const;
  void updateFilePathForSelection(int index, const QList<int> &subcollections);
  void trySelectWidget(int index, const QList<int> &subcollections, int attempt);
  void handleSuccessfulSelection(int index);
  void centerItemVertically(int index, bool immediate);
  void ensureHorizontallyVisible(int index);
  [[nodiscard]] bool handleSlashKey();
  [[nodiscard]] bool handleEscapeKey();

  // Gamepad input arrives via signals/timers and never crosses
  // EventManager::filterEvent, so the modal gates that swallow keyboard and
  // mouse grid input while a dialog is up must be re-checked in the
  // gamepad-driven slots. Delegates to EventManager::modalInputGateActive() —
  // the single definition of the predicate (active modal widget OR visible
  // scrape-result dialog). Dialog-owned gamepad flows (binding capture) run
  // on GamepadManager's bindingCaptureButtonPressed path, which bypasses
  // these slots and stays live while gated.
  [[nodiscard]] bool modalInputGateActive() const;

  [[nodiscard]] int resolveDoubleClickIndexCandidate() const;
  [[nodiscard]] QString derivePathFromIndex(int idx) const;
  [[nodiscard]] int resolveOwnerForPath(const QString &path) const;
  [[nodiscard]] int getFallbackCollectionIndex() const;

  // Expand-mode helper: returns true when the activation was consumed by the
  // expand path (artwork preview shown) and the caller must NOT launch.
  // Returns false when the caller should proceed with launch.
  [[nodiscard]] bool maybeExpandInsteadOfLaunch(const QString &filePath, int collectionIndex,
                                                int activationIndex);

  // ─────────────────────────────────────────────────────────────────────────
  // DESTRUCTION-ORDER ANCHOR (Kartend-wxtx6 / Kartend-gutqx).
  // The declaration order of the owned-sub-manager unique_ptr members below is
  // LOAD-BEARING: they are destroyed in reverse declaration order after
  // ~InteractionManager's body runs, and ~InteractionManager
  // (interactionmanager.cpp) detaches its event filters expecting m_eventManager
  // and friends to still be alive at that point. It is also mirrored, reversed,
  // by ApplicationManager::destroyManagersAndClearContextSlots()
  // (applicationmanager.cpp), which nulls the matching ctx->managers.* slots
  // before m_interactionManager.reset(). Reordering these members can silently
  // re-introduce the UBSan/vptr teardown bug history under Kartend-gutqx — if
  // you reorder, re-read ~InteractionManager and destroyManagersAndClearContextSlots().
  // ─────────────────────────────────────────────────────────────────────────

  // Search delegation (owned helper)
  std::unique_ptr<SearchManager> m_searchManager;

  // Selection delegation (owned helper)
  std::unique_ptr<SelectionManager> m_selectionManager;

  // Keyboard delegation (owned helper)
  std::unique_ptr<KeyboardManager> m_keyboardManager;

  // Gamepad delegation (owned helper)
  std::unique_ptr<GamepadManager> m_gamepadManager;

  // Arrow key navigation delegation (owned helper)
  std::unique_ptr<ArrowNavigationHandler> m_arrowHandler;

  // Alphabetic navigation delegation (owned helper)
  std::unique_ptr<AlphabeticNavigationHandler> m_alphabeticHandler;

  // Animation delegation (owned helper)
  std::unique_ptr<AnimationManager> m_animationManager;

  // Mouse hold scrolling delegation (owned helper)
  std::unique_ptr<MouseManager> m_mouseManager;

  // Launch delegation (owned helper)
  std::unique_ptr<LaunchManager> m_launchManager;

  // Viewport delegation (owned helper)
  std::unique_ptr<ViewportManager> m_viewportManager;

  // Event handling delegation (owned helper)
  std::unique_ptr<EventManager> m_eventManager;

  // Attract mode delegation (owned helper)
  std::unique_ptr<AttractManager> m_attractManager;

  // ctx is the single source of truth for sibling managers. Inline accessors
  // below are the canonical read path; never cache sibling-manager pointers
  // as direct fields.
  /// Sections the focus chord/right stick can move between, resolved from
  /// the live focus widget.
  enum class FocusSection { Grid, Tree, Pane, Toolbar };
  struct FocusSectionInfo {
    QWidget *widget = nullptr;
    QString label;
    FocusSection kind = FocusSection::Grid;
  };
  [[nodiscard]] FocusSectionInfo currentFocusSection() const;
  /// The details pane's scrollable viewport, or null when the pane is
  /// hidden or has nothing to scroll.
  [[nodiscard]] QAbstractScrollArea *detailsPaneScrollArea() const;
  /// Step focus across the top bar's visible buttons in layout order.
  void moveToolbarFocus(int delta);
  /// Synthesize a key press/release on the focused widget so the section
  /// handles it with its own native behaviour.
  void sendKeyToFocusedWidget(int key);

  /// Modifier HUD, created lazily and parented to the items page.
  QPointer<FocusSectionOverlay> m_focusOverlay;
  /// Shared pulsing ring: the pane's selected region normally, the focused
  /// section while the modifier is held.
  QPointer<SelectionIndicator> m_selectionIndicator;
  /// True while the section modifier (Select) is held — the stick is then
  /// purely a section switcher. Unheld, vertical belongs to the details
  /// pane except for the grid↔toolbar hop (user request 2026-08-24:
  /// up from the grid focuses the toolbar, down returns).
  bool m_focusModifierHeld = false;
  /// Returns the right stick's focus to the grid when the user stops
  /// driving the pane (user request 2026-08-18: one second of no input).
  QTimer *m_paneIdleTimer = nullptr;
  /// Index into paneRegions() of the region the right stick is driving.
  int m_paneRegionIndex = -1;
  /// True once the right stick has taken over the pane: confirm then acts
  /// on the ringed pane target instead of launching the grid selection.
  /// Cleared the moment the d-pad/left stick pulls focus back to the grid,
  /// so A never opens artwork when the user is browsing the library.
  bool m_paneSelectionActive = false;
  /// Debounce for tree-row auto-activation (user request 2026-08-18:
  /// highlighting a collection with the stick should switch to it, with no
  /// second button press). Short enough to feel immediate, long enough
  /// that skimming past ten rows does not load ten collections.
  QTimer *m_treeActivateTimer = nullptr;

  /// The pane's drivable targets, ordered as they appear: individual
  /// artwork thumbnails first (each separately selectable), then the
  /// description and metadata regions.
  [[nodiscard]] QList<QWidget *> paneRegions() const;
  void showSelectionIndicatorFor(QWidget *target);
  void hideSelectionIndicator();

  const ApplicationContext *m_ctx = nullptr;
  // Kartend-h1l8f: keeps the IScrollManager facade — its partials span five
  // scroll roles (data, grid, overlay, preview, search).
  [[nodiscard]] IScrollManager *scrollMgr() const {
    return m_ctx ? m_ctx->scrollManager() : nullptr;
  }
  [[nodiscard]] IDetailsPaneManager *detailsPaneMgr() const {
    return m_ctx ? m_ctx->detailsPaneManager() : nullptr;
  }
  [[nodiscard]] IDetailPageManager *detailPageMgr() const {
    return m_ctx ? m_ctx->detailPageManager() : nullptr;
  }
  [[nodiscard]] ISettingsManager *settingsMgr() const {
    return m_ctx ? m_ctx->settingsManager() : nullptr;
  }
  [[nodiscard]] IDatabaseManager *databaseMgr() const {
    return m_ctx ? m_ctx->databaseManager() : nullptr;
  }
  [[nodiscard]] INavigationManager *navMgr() const {
    return m_ctx ? m_ctx->navigationManager() : nullptr;
  }
  [[nodiscard]] class IPlaylistManager *playlistMgr() const {
    return m_ctx ? m_ctx->playlistManager() : nullptr;
  }
  [[nodiscard]] ISessionManager *sessionMgr() const {
    return m_ctx ? m_ctx->sessionManager() : nullptr;
  }
  [[nodiscard]] IArtworkManager *artworkMgr() const {
    return m_ctx ? m_ctx->artworkManager() : nullptr;
  }
  // Borrowed UI widgets from MainWindow. QPointer guards against dangling
  // reads if MainWindow ever reconstructs a layout-level widget (live theme
  // reload, full-screen swap) and the underlying QWidget is destroyed before
  // InteractionManager (Kartend-ccl0).
  QPointer<QScrollArea> m_itemScrollArea;
  QPointer<QWidget> m_gridContainer;
  QPointer<QStackedWidget> m_stackedWidget;
  QPointer<QWidget> m_itemsPage;
  QList<CollectionConfig> *m_collections = nullptr;
  const int *m_currentCollectionIndex = nullptr;
  QPointer<QLineEdit> m_searchBar;
  GeneralSettings *m_generalSettings = nullptr;
  const bool *m_isShuttingDown = nullptr;
  /// Canonical shutdown test (Kartend-kalh1, mirrors GamepadManager):
  /// dereferences the MainWindow-owned flag pointer and folds in the
  /// app-global closingDown. Use this — never truth-test m_isShuttingDown,
  /// which is non-null for the manager's whole wired life and would read
  /// permanently true (the stopRepeat bug this rule comes from).
  [[nodiscard]] bool shuttingDown() const;

  // Kartend-n8kh: owner-supplied dialog runners. The closures live in the
  // UI layer (MainWindow setup wiring), so the input layer never #includes
  // the dialog headers. The edit-metadata and smart-playlist runners are NOT
  // stored here — setupReferences forwards each straight into its only
  // consumer (m_itemMetadataActions / m_playlistMenu, Kartend-5lmt7).
  LaunchPreviewDialogRunner m_runLaunchPreviewDialog;
  // Kartend-sqoq0: generic stock-modal runners (warn for the
  // launcher-unavailable gate, getOpenFileName for the manual-file picker).
  // Null runners fall back to direct QMessageBox / QFileDialog construction.
  DialogRunners m_dialogs;

  // Item-metadata mutation handlers (edit dialog, manual path, launcher
  // override, pin/hide/continue-later), extracted from this class's
  // context-action partial (Kartend-5lmt7). Callers (context-menu lambdas,
  // MainWindow's edit entry points) use the controller directly via the
  // itemMetadataActions() accessor (Kartend-i5ai0).
  std::unique_ptr<ItemMetadataActionController> m_itemMetadataActions;
  // Playlist context-menu actions (create/add/rename/delete, smart-playlist
  // dialogs, import/export), same extraction (Kartend-5lmt7).
  std::unique_ptr<PlaylistMenuController> m_playlistMenu;

  void scheduleScrollbarRecovery();
  QMetaObject::Connection m_scrollbarRecoveryConn;

  // Signal connection helpers for setupReferences
  void connectSearchManagerSignals();
  void connectSelectionManagerSignals();
  void connectKeyboardManagerSignals();
  void connectGamepadManagerSignals();
  void connectAnimationManagerSignals();
  void connectMouseManagerSignals();
  void connectViewportManagerSignals();
  void connectEventManagerSignals();
  void connectAttractManagerSignals();

  // Setup helpers (split from setupReferences)
  void setupArrowNavigationHandler(const InteractionManagerSetup &setup);
  void setupAlphabeticNavigationHandler(const InteractionManagerSetup &setup);
  void installEventFilters();

  void applySelectionStateForIndex(int idx);
  void finalizeRestoreFlagsAndFocus();
  void scheduleSidebarMetadataUpdateIfVisible(int targetIndex, int initialDelayMs,
                                              int secondaryDelayMs);
  [[nodiscard]] QString titleForIndexInColl(int coll, int idx) const;
  void persistSelectionForIndex(int coll, int idx);

  void setPendingSelectionIfNeeded(bool condition, int newSelection);
  void updateSelectionStateAfterMove(int newSelection);
  auto processEnterOrReturnKey(int totalItems) -> bool;
  auto handleEnterOnSubcollection(int subActualIndex, int subCollIdx) -> bool;
  auto handleEnterOnVirtualFolder(const QString &folderPath) -> bool;
  [[nodiscard]] auto handleEnterOnItem(int currentSelection, int totalItems) -> bool;
  [[nodiscard]] auto isItemOffscreen(int selection, int gridWidth) const -> bool;
  void applyMinorHorizontalSuppress();

  // Selection helpers
  void persistSuppressedSelectionAndMaybeCenter(int index, const QList<int> &subcollections,
                                                bool skipCenter);
};

#endif