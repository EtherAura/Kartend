#ifndef TOOLBARCONTROLLER_H
#define TOOLBARCONTROLLER_H

#include "collection/generalsettings.h"
#include "collection/searchpreset.h"
#include <functional>
#include <QHash>
#include <QList>
#include <QObject>

class ApplicationManager;
class INavigationManager;
class ISettingsManager;
class MainWindow;
class QAction;
class QLineEdit;
class QMenu;
class QToolButton;
struct GeneralSettings;

/// Owns the items-page toolbar's stateful Qt widgets and the setup/sync/refresh
/// glue that drives them: the layout-picker QToolButton + its four QActions
/// (Grid / List / Cover Flow / Horizontal), the search-mode QAction embedded
/// inside the search QLineEdit, and the consolidated filter QToolButton + its
/// type-radio group / title-pattern toggle / pattern editor.
///
/// Coupling: the helper takes a MainWindow* and reads/writes its m_collections,
/// m_generalSettings, currentCollectionIndex, plus the SettingsManager /
/// NavigationManager getters. The helper is parented to its MainWindow so
/// destruction order is well-defined.
class ToolbarController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ToolbarController)

public:
  struct Setup {
    MainWindow *mainWindow = nullptr;
    QToolButton *viewModeButton = nullptr;
    QToolButton *filterButton = nullptr;
    QToolButton *homeButton = nullptr;
    QToolButton *collectionWarningBadge = nullptr;
    QLineEdit *searchBar = nullptr;
    /// Opens the saved-filter manager (Kartend-w4knq). Supplied as a closure
    /// rather than called on MainWindow directly because every other
    /// *Interactive() dialog launcher is reached this way — see
    /// MenuController's onPresentationProfiles — which is what keeps those
    /// launchers private to MainWindow.
    std::function<void()> onManageSearchPresets;
  };

  explicit ToolbarController(QObject *parent = nullptr);

  /// Wire the helper to the QToolButton / QLineEdit references it'll drive.
  /// Safe to call once after construction.
  void initialize(const Setup &setup);

  /// Build the layout-picker popup menu attached to viewModeButton — four
  /// checkable actions that call MainWindow::setViewType.
  void setupViewModeButton();

  /// Reflect @p viewType onto the layout-picker: tick the matching action and
  /// refresh the button's tooltip.
  void syncViewModeButton(ViewType viewType);

  /// Reflect the ACTIVE collection's persisted viewType onto the layout-picker.
  /// No-op when no collection is current (root/home view), which leaves the
  /// previous checked state rather than inventing one. Wired to the popup's
  /// aboutToShow so the menu can never disagree with the view (Kartend-lp7j9).
  void syncViewModeButtonFromCurrentCollection();

  /// Create the search-mode QAction and attach it to the search QLineEdit at
  /// LeadingPosition. Wiring the triggered() signal to InteractionManager
  /// happens later (in the wiring pass) once that manager exists.
  void setupSearchModeAction();

  /// Wire the home button's clicked() signal to NavigationManager::loadRootView.
  /// Idempotent — safe to call after the navigation manager is alive.
  void setupHomeButton();

  /// Refresh the home button's icon and visibility from the current settings:
  /// visible iff useHomeView is on, icon is the user's homeViewIcon path
  /// (falling back to the themed go-home icon).
  void refreshHomeButton(const GeneralSettings &gs);

  /// Wire the collection-warning badge: install the click handler that opens
  /// Settings → Launchers and seed the icon to SP_MessageBoxWarning. The
  /// badge stays hidden until refreshCollectionWarningBadge() finds an
  /// unresolvable launcher path on the active collection.
  void setupCollectionWarningBadge();

  /// Recompute the badge state for the currently-active collection. Hides
  /// the badge when the collection's launcher profile is clean; otherwise
  /// shows it with a one-issue-per-line tooltip (delegating to the
  /// launch-time LauncherUtils::launcherPathIssues overload — preset-resolved
  /// + %collection%-expanded, the same verdict the pre-launch gate reaches —
  /// so the tooltip text matches the sidebar's `⚠ Launcher path` rows
  /// verbatim).
  void refreshCollectionWarningBadge();

  /// Apply the per-button visibility flags from @p gs to the toolbar's
  /// remaining user-toggleable widgets (filter button + search bar).
  void applyToolbarCustomization(const GeneralSettings &gs);

  /// Wire the filter button (idempotent — installs the QMenu::triggered
  /// handler on first call, rebuilds the popup on every call).
  void connectFilterToolbar();

  /// Rebuild the filter button popup from the current collection set + active
  /// collection's title-exclusion state. Called on startup, on every
  /// collection switch, and after settings edits that may have added or
  /// removed type tags.
  void refreshFilterToolbar();

  /// Open the modal regex editor for the current collection's
  /// title-exclusion patterns.
  void showTitleFilterEditor();

  /// Drop the in-memory saved-filter cache so the next popup rebuild re-reads
  /// the registry from disk. Called after the manage dialog closes, which may
  /// have added or deleted presets behind this controller's back.
  void invalidateSearchPresetCache();

  /// Search-mode QAction (lives inside the search QLineEdit). Exposed so the
  /// wiring pass can connect its triggered() signal to InteractionManager
  /// once that manager exists.
  [[nodiscard]] QAction *searchModeAction() const { return m_searchModeAction; }

private:
  // Cached helpers: avoid re-chaining mainWindow→applicationManager→XxxManager
  // on every menu rebuild and click. Populated lazily through
  // applicationManager() / settingsMgr() / navMgr() since at initialize() time
  // the ApplicationManager may not have wired its child managers yet.
  [[nodiscard]] ApplicationManager *applicationManager() const;
  [[nodiscard]] ISettingsManager *settingsMgr() const;
  [[nodiscard]] INavigationManager *navMgr() const;

  MainWindow *m_mainWindow = nullptr;
  QToolButton *m_viewModeButton = nullptr;
  QToolButton *m_filterButton = nullptr;
  QToolButton *m_homeButton = nullptr;
  QToolButton *m_collectionWarningBadge = nullptr;
  QLineEdit *m_searchBar = nullptr;
  std::function<void()> m_onManageSearchPresets;

  QAction *m_searchModeAction = nullptr;
  QAction *m_viewActionGrid = nullptr;
  QAction *m_viewActionList = nullptr;
  QAction *m_viewActionCoverFlow = nullptr;
  QAction *m_viewActionHorizontal = nullptr;
  /// Cleared every refresh() — null between rebuilds.
  QAction *m_titleFilterEnabledAction = nullptr;

  /// Per-action role for the filter-menu QActions. Replaces the
  /// QObject::setProperty("filterRole", ...) dynamic-property pattern
  /// the InteractionStateHolder migration was supposed to eradicate
  /// (Kartend-0zhz). Refilled on every rebuildFilterMenu(); cleared
  /// at the start of that rebuild so dangling QAction* keys can't
  /// outlive the parent QMenu.
  enum class FilterRole { Type, TitleToggle, TitleEdit, SavePreset, ApplyPreset, ManagePresets };
  QHash<QAction *, FilterRole> m_filterRoles;

  /// Kartend-w4knq: append the saved-filter section to the filter popup —
  /// one entry per preset, plus Save-current and Manage.
  void appendSavedFilterSection(QMenu *menu);
  /// Snapshot the live view state under a user-supplied name and persist it.
  void saveCurrentSearchAsPreset();
  /// Read the registry from disk on first use and cache it. The popup is
  /// rebuilt on every collection switch, so re-reading each time would put a
  /// file read on a path that has none today.
  void ensureSearchPresetsLoaded();
  /// Write the cache back to disk, warning through the main window on failure
  /// — a preset the user named and did not get is worth saying out loud.
  void persistSearchPresets();

  QList<SearchPreset> m_searchPresets;
  bool m_searchPresetsLoaded = false;
};

#endif // TOOLBARCONTROLLER_H
