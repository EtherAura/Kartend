#ifndef INTERACTIONMANAGER_H
#define INTERACTIONMANAGER_H

#include "applicationcontext.h"
#include "collectionutils.h"
#include "interactionstateholder.h" // Required: m_state is a value member
#include "setuputils.h"
#include <memory>
#include <QObject>
#include <QPointer>
#include <QScrollArea>

QT_BEGIN_NAMESPACE
class QKeyEvent;
class QLineEdit;
class QPushButton;
class QStackedWidget;
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

class ItemWidget;
class DatabaseManager;
class NavigationManager;
class SettingsManager;
class SidebarManager;
class ScrollManager;
class SessionManager;
class ArtworkManager;
class MetadataSidebar;

/**
 * @brief Setup struct for InteractionManager dependencies.
 *
 * Fields can be set individually, or common fields can be populated
 * from an ApplicationContext via the ctx pointer.
 */
struct InteractionManagerSetup {
  // Optional: shared context for common fields
  const ApplicationContext *ctx = nullptr;

  // Manager dependencies (can be overridden or taken from ctx)
  ScrollManager *scrollManager = nullptr;
  SidebarManager *sidebarManager = nullptr;
  SettingsManager *settingsManager = nullptr;
  DatabaseManager *databaseManager = nullptr;
  NavigationManager *navigationManager = nullptr;
  SessionManager *sessionManager = nullptr;
  ArtworkManager *artworkManager = nullptr;

  // UI elements (can be overridden or taken from ctx)
  MetadataSidebar *sidebar = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *collectionPage = nullptr;
  QLineEdit *searchBar = nullptr;
  QPushButton *searchModeButton = nullptr;

  // State references (can be overridden or taken from ctx)
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  GeneralSettings *generalSettings = nullptr;
  const bool *isShuttingDown = nullptr;
  const CollectionHierarchyCache *hierarchyCache = nullptr;

  // Manager accessors that check ctx fallback
  SETUP_GETTER_INLINE_MGR_SAME(ScrollManager *, ScrollManager, scrollManager)
  SETUP_GETTER_INLINE_MGR_SAME(SidebarManager *, SidebarManager, sidebarManager)
  SETUP_GETTER_INLINE_MGR_SAME(SettingsManager *, SettingsManager, settingsManager)
  SETUP_GETTER_INLINE_MGR_SAME(DatabaseManager *, DatabaseManager, databaseManager)
  SETUP_GETTER_INLINE_MGR_SAME(NavigationManager *, NavigationManager, navigationManager)
  SETUP_GETTER_INLINE_MGR_SAME(SessionManager *, SessionManager, sessionManager)
  SETUP_GETTER_INLINE_MGR_SAME(ArtworkManager *, ArtworkManager, artworkManager)

  // UI element accessors that check ctx fallback
  SETUP_GETTER_INLINE_UI_SAME(QScrollArea *, ItemScrollArea, itemScrollArea)
  SETUP_GETTER_INLINE_UI_SAME(QWidget *, GridContainer, gridContainer)
  SETUP_GETTER_INLINE_UI_SAME(MetadataSidebar *, Sidebar, sidebar)
  SETUP_GETTER_INLINE_UI_SAME(QStackedWidget *, StackedWidget, stackedWidget)
  SETUP_GETTER_INLINE_UI_SAME(QWidget *, ItemsPage, itemsPage)
  SETUP_GETTER_INLINE_UI_SAME(QLineEdit *, SearchBar, searchBar)
  SETUP_GETTER_INLINE_UI_SAME(QPushButton *, SearchModeButton, searchModeButton)
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
 * - Does NOT own: m_scrollManager, m_sidebarManager, m_settingsManager,
 * m_databaseManager, m_navigationManager, m_sessionManager, m_artworkManager,
 * UI widgets (borrowed references)
 *
 * Sub-managers are registered in ApplicationContext after setup for sibling
 * access.
 */
class InteractionManager : public QObject {
  Q_OBJECT
public:
  explicit InteractionManager(QObject *parent = nullptr);
  ~InteractionManager() override;
  void setupReferences(const InteractionManagerSetup &setup);

  // ─────────────────────────────────────────────────────────────────────────
  // Owned sub-manager accessors - allows ApplicationContext to register them
  // ─────────────────────────────────────────────────────────────────────────
  [[nodiscard]] AnimationManager *animationManager() const { return m_animationManager.get(); }
  [[nodiscard]] AttractManager *attractManager() const { return m_attractManager.get(); }
  [[nodiscard]] SelectionManager *selectionManager() const { return m_selectionManager.get(); }
  [[nodiscard]] ViewportManager *viewportManager() const { return m_viewportManager.get(); }
  [[nodiscard]] MouseManager *mouseManager() const { return m_mouseManager.get(); }
  [[nodiscard]] KeyboardManager *keyboardManager() const { return m_keyboardManager.get(); }
  [[nodiscard]] GamepadManager *gamepadManager() const { return m_gamepadManager.get(); }
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
  void clearSelectionAndFocus();
  [[nodiscard]] int currentSelectedIndex() const;
  [[nodiscard]] int getCurrentGridWidth() const;
  void toggleSearchMode();
  void updateSearchModeButton();
  void updateSearchBarPlaceholder();
  void launchItemWithCollection(const QString &filePath, int collectionIndex);
  [[nodiscard]] bool isWheelScrolling() const;
  auto eventFilter(QObject *obj, QEvent *event) -> bool override;
  auto handleGlobalKeyPress(QKeyEvent *event) -> bool;
  [[nodiscard]] ItemWidget *getSelectedMediaItem() const;
  void setSelectedMediaItem(ItemWidget *widget);
  [[nodiscard]] QString selectedFilePath() const;
  void ensureItemVisible(int index, bool allowHorizontalScroll);
  void applyImmediateViewportPositioningForSelection(int targetIndex);
  void initializeSearchModeForCurrentCollection();
  void beginSelectionRestore(int targetIndex);
  void cancelPendingSelectionRestore();
  void resetSelectionRestoreState(); // Reset for new navigation (allows
                                     // auto-restore)
  void stopRepeat(bool suppressRecentering = false);
  // Stops any running scroll animations (wheel/arrow key) to prevent stale
  // animations from applying after a view rebuild (e.g., entering a subfolder).
  void stopScrollAnimations();
  // Shows a right-click context menu for the item at the given visual index.
  void showContextMenu(ItemWidget *widget, int visualIndex, const QPoint &globalPos);
  // Opens the custom-fields editor for the given media item (Kartend-hpln).
  // Persists changes via DatabaseManager::saveItemMetadata() and refreshes
  // the sidebar so new fields render immediately.
  void editCustomFields(const QString &filePath, const QString &itemName);
  // Sets or clears the per-item manual override for a media item
  // (Kartend-9jdv). Stored in `item_metadata.manual_path`; passing an empty
  // path clears the override and re-enables auto-discovery in the
  // collection's manualDirectory. Refreshes the sidebar afterwards.
  void setItemManualPath(const QString &filePath, const QString &manualPath);
  [[nodiscard]] bool isRestoringSelection() const;
  [[nodiscard]] int targetRestoreIndex() const;
  [[nodiscard]] bool forceImmediateCenter() const;
  void recenterCurrentSelection();

  // Navigation progress state - set by NavigationManager during collection
  // switches
  [[nodiscard]] bool isNavigationInProgress() const { return m_navigationInProgress; }
  void setNavigationInProgress(bool inProgress) { m_navigationInProgress = inProgress; }

  // Centralized interaction state holder - provides typed access to state
  // that was previously stored as Qt dynamic properties
  [[nodiscard]] InteractionStateHolder &state() { return m_state; }
  [[nodiscard]] const InteractionStateHolder &state() const { return m_state; }

private:
  bool m_navigationInProgress = false;
  InteractionStateHolder m_state;

  // Selection restore token for cancellation - kept here as it coordinates with
  // NavigationManager
  int m_selectionRestoreToken = 0;
  bool m_selectionRestorePending = false;

signals:
  void selectionChanged(int index);

public slots:
  void saveCurrentSelection();
  void handleImmediateSearchTextChanged(const QString &text);
  /// Triggered when the user activates (Enter / double-click) while the
  /// artwork preview overlay is visible. Hides the overlay, clears expand
  /// state, and launches the previewed item (falling back to the current
  /// selection when @p filePath is empty, e.g. for sidebar gallery
  /// previews that don't carry a media path).
  void onArtworkPreviewLaunchRequested(const QString &filePath = QString());
  /// Triggered when the user middle-clicks a grid/list item (Kartend-ljey).
  /// Opens a video-first preview overlay for the clicked item without
  /// changing selection or starting the expand-mode launch sequence.
  void onMediaPreviewRequested(ItemWidget *widget, int visualIndex);

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

  ScrollManager *m_scrollManager = nullptr;
  SidebarManager *m_sidebarManager = nullptr;
  SettingsManager *m_settingsManager = nullptr;
  DatabaseManager *m_databaseManager = nullptr;
  NavigationManager *m_navigationManager = nullptr;
  SessionManager *m_sessionManager = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QWidget *m_itemsPage = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;
  QLineEdit *m_searchBar = nullptr;
  GeneralSettings *m_generalSettings = nullptr;
  const bool *m_isShuttingDown = nullptr;

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
  void setupAlphabeticNavigationHandler();
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