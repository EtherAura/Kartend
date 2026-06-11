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
  int *currentCollectionIndex = nullptr;
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
  SETUP_GETTER_INLINE_COL_SAME(int *, CurrentCollectionIndex, currentCollectionIndex)
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
class InteractionManager : public QObject, public IInteractionManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(InteractionManager)
public:
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
  [[nodiscard]] ItemWidget *getSelectedMediaItem() const;
  void setSelectedMediaItem(ItemWidget *widget);
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
  // Opens the per-item metadata editor (notes, tags, rating, source URL,
  // custom fields). Persists changes via DatabaseManager::saveItemMetadata()
  // and refreshes the sidebar so new fields render immediately.
  void editItemMetadata(const QString &filePath, const QString &itemName);

  // Opens the read-only launch-preview / dry-run dialog. Resolves the
  // launcher (incl. preset + per-item override), builds the command via
  // LaunchManager::previewLaunchCommand, and hands the result to the
  // owner-supplied dialog runner. No external process is spawned.
  void previewLaunchCommand(const QString &filePath, const QString &itemName);
  // Sets or clears the per-item manual override for a media item
  // Stored in `item_metadata.manual_path`; passing an empty
  // path clears the override and re-enables auto-discovery in the
  // collection's manualDirectory. Refreshes the sidebar afterwards.
  void setItemManualPath(const QString &filePath, const QString &manualPath);
  // Sets or clears the per-item launcher override. Pass an
  // index into the owning collection's unified launcher list (0 = primary,
  // 1..N = launcher.additionalLaunchers[0..N-1]) to pin a launcher; pass -1 to clear
  // the override and re-enable the multi-launcher chooser at launch.
  void setItemLauncherOverride(const QString &filePath, int launcherIndex);

  // Per-item state flag toggles (item_metadata.is_pinned / is_hidden /
  // continue_later). Each reads the current row, flips the relevant flag,
  // stamps source='user', persists, and refreshes the sidebar so the new
  // state surfaces immediately.
  void toggleItemPinned(const QString &filePath);
  void toggleItemHidden(const QString &filePath);
  void toggleItemContinueLater(const QString &filePath);

  // Builds the live (displayName, uuid) list fed to the smart-playlist
  // dialog's ByCollection picker. Playlists are skipped because anchoring
  // a smart filter on a playlist's synthetic uuid would yield a recursive
  // dependency. Built fresh on each open so collection renames / additions
  // are picked up.

  // ─── Playlist context-menu handlers ────────────────────────
  // Prompts for a playlist name, creates the playlist, and adds the given
  // (srcUuid, filePath) reference. Cancelling the prompt is a no-op; an empty
  // reference creates an empty playlist (useful for "set up first, fill
  // later" workflows).
  void addItemToNewPlaylist(const QString &srcUuid, const QString &filePath);
  // Idempotent add — duplicates are silently rejected by PlaylistManager.
  void addItemToPlaylist(const QString &playlistId, const QString &srcUuid,
                         const QString &filePath);
  // Smart-playlist counterpart of addItemToNewPlaylist. Pops the
  // CreateSmartPlaylistDialog and creates a filter-driven playlist on
  // accept; cancellation is a no-op. The current item context is
  // intentionally ignored — smart playlists derive their members from
  // the filter, not from the right-clicked item.
  void createSmartPlaylistDialog();
  // Open the create dialog pre-loaded with an existing smart playlist's
  // filter so the user can edit the criterion + parameters in place.
  // Called from the Edit smart filter… action that appears inside a
  // smart playlist view.
  void editSmartPlaylistDialog(const QString &playlistId, const QString &currentName);
  // Inline-rename via a single QInputDialog. No-ops on cancel or unchanged
  // name; the rename is always reflected in the sidebar via the
  // playlistsChanged → resyncPlaylistCollections chain.
  void renamePlaylistDialog(const QString &playlistId, const QString &currentName);
  // Confirms (the cascade can't be undone) then deletes the playlist row plus
  // all of its items. Source items in their owning collections are untouched.
  void deletePlaylistConfirm(const QString &playlistId, const QString &currentName);

  // ─── Playlist import / export ──────────────────────────────
  // Pops a save-file dialog and writes the playlist as JSON or M3U via
  // PlaylistManager. M3U is a basic dialect (#EXTM3U + path-per-line); JSON
  // is the lossless Kartend format that round-trips through importFromJson.
  void exportPlaylistToFile(const QString &playlistId, const QString &currentName, bool asJson);
  // Pops an open-file dialog and creates a new playlist from the chosen file.
  // Format is auto-detected by extension (.json → JSON, anything else → M3U).
  // M3U entries that don't resolve to a known item are skipped, with the
  // count surfaced in a single completion message-box so the user knows
  // why their imported playlist may be shorter than the source.
  void importPlaylistFromFile();
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

  [[nodiscard]] int resolveDoubleClickIndexCandidate() const;
  [[nodiscard]] QString derivePathFromIndex(int idx) const;
  [[nodiscard]] int resolveOwnerForPath(const QString &path) const;
  [[nodiscard]] int getFallbackCollectionIndex() const;

  // Expand-mode helper: returns true when the activation was consumed by the
  // expand path (artwork preview shown) and the caller must NOT launch.
  // Returns false when the caller should proceed with launch.
  [[nodiscard]] bool maybeExpandInsteadOfLaunch(const QString &filePath, int collectionIndex,
                                                int activationIndex);

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
  const ApplicationContext *m_ctx = nullptr;
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
  int *m_currentCollectionIndex = nullptr;
  QPointer<QLineEdit> m_searchBar;
  GeneralSettings *m_generalSettings = nullptr;
  const bool *m_isShuttingDown = nullptr;

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
  // context-action partial (Kartend-5lmt7). The public methods of the same
  // names on this class are one-line delegates so existing callers
  // (context-menu lambdas, MainWindow's edit entry points) are unchanged.
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