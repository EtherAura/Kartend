#ifndef SEARCHMANAGER_H
#define SEARCHMANAGER_H

#include "collection/collectionconfig.h"
#include "collection/collectionhierarchycache.h"
#include "collection/generalsettings.h"
#include "searchutils.h"
#include "setuputils.h"
#include "timerutils.h"
#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QAction;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTimer;
class QStackedWidget;
QT_END_NAMESPACE

class IDatabaseManager;
class InteractionStateHolder;
class INavigationManager;
class IScrollManager;
class ISettingsManager;
#include "applicationcontext_fwd.h"

struct SearchManagerSetup {
  const ApplicationContext *ctx = nullptr;

  // UI / collection-state references — sibling managers are read directly from
  // ctx at runtime, never duplicated here.
  QLineEdit *searchBar = nullptr;
  QAction *searchModeAction = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *collectionPage = nullptr;
  QWidget *itemsPage = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  const int *currentCollectionIndex = nullptr;
  const CollectionHierarchyCache *hierarchyCache = nullptr;

  SETUP_GETTER_DECL(QLineEdit *, SearchBar)
  SETUP_GETTER_DECL(QAction *, SearchModeAction)
  SETUP_GETTER_DECL(QScrollArea *, ItemScrollArea)
  SETUP_GETTER_DECL(QStackedWidget *, StackedWidget)
  SETUP_GETTER_DECL(QWidget *, ItemsPage)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
  SETUP_GETTER_DECL(const int *, CurrentCollectionIndex)
  SETUP_GETTER_DECL(const CollectionHierarchyCache *, HierarchyCache)
};

class SearchManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SearchManager)
public:
  explicit SearchManager(QObject *parent = nullptr);
  ~SearchManager() override;

  void setupReferences(const SearchManagerSetup &setup);

  // Mode management
  [[nodiscard]] SearchMode currentMode() const { return m_currentSearchMode; }
  void setCurrentMode(SearchMode mode) { m_currentSearchMode = mode; }
  void toggleSearchMode();
  void updateSearchModeButton();
  void updateSearchBarPlaceholder();
  void initializeSearchModeForCurrentCollection();

  // Search text handling
  void onSearchTextChanged(const QString &text, int currentSelectedIndex);
  void performDebouncedSearch();

  // Search state
  [[nodiscard]] bool isSearchActive() const { return m_searchActive; }
  void setSearchActive(bool active) { m_searchActive = active; }
  [[nodiscard]] const QString &currentSearchText() const { return m_currentSearchText; }
  void setCurrentSearchText(const QString &text) { m_currentSearchText = text; }

  // Pre-search state preservation
  [[nodiscard]] int preSearchCollectionIndex() const { return m_preSearchCollectionIndex; }
  void setPreSearchCollectionIndex(int idx) { m_preSearchCollectionIndex = idx; }
  [[nodiscard]] SearchMode preSearchMode() const { return m_preSearchMode; }
  void setPreSearchMode(SearchMode mode) { m_preSearchMode = mode; }
  [[nodiscard]] int preSearchSelectedIndex() const { return m_preSearchSelectedIndex; }
  void setPreSearchSelectedIndex(int idx) { m_preSearchSelectedIndex = idx; }

  // Timer access for InteractionManager
  [[nodiscard]] TimerUtils::DebouncedTimer *debounceTimer() const { return m_searchDebounceTimer; }
  QMetaObject::Connection &itemsLoadedConnection() { return m_searchItemsLoadedConn; }

  // Helpers
  [[nodiscard]] SearchContext computeSearchContext() const;
  [[nodiscard]] QVector<SearchMode> buildSearchModeCycle(const SearchContext &ctx) const;
  [[nodiscard]] bool hasDirectItemsForIndex(int idx) const;
  [[nodiscard]] bool allowAllFor(const CollectionConfig &cfg, int collIndex, bool hasSubs) const;

signals:
  void searchModeChanged(SearchMode mode);
  void requestClearSelection();
  void requestSelectionRestore(int index);
  void requestScrollbarRecovery();

private:
  void scheduleSearchBarRefocusIfNeeded();
  // Reclaim focus for the search bar, but only from transient/non-input
  // grabbers — never yank it back from a text field the user deliberately
  // clicked into (Kartend-8oau).
  void refocusSearchBarUnlessDeliberate();

  // ctx is the single source of truth for sibling managers + state.
  const ApplicationContext *m_ctx = nullptr;
  [[nodiscard]] InteractionStateHolder *state() const {
    return m_ctx ? m_ctx->interactionState() : nullptr;
  }
  [[nodiscard]] IDatabaseManager *databaseMgr() const {
    return m_ctx ? m_ctx->databaseManager() : nullptr;
  }
  [[nodiscard]] INavigationManager *navMgr() const {
    return m_ctx ? m_ctx->navigationManager() : nullptr;
  }
  // Kartend-h1l8f: keeps the IScrollManager facade — spans three scroll roles
  // (search state, lifecycle, data).
  [[nodiscard]] IScrollManager *scrollMgr() const {
    return m_ctx ? m_ctx->scrollManager() : nullptr;
  }
  [[nodiscard]] ISettingsManager *settingsMgr() const {
    return m_ctx ? m_ctx->settingsManager() : nullptr;
  }

  GeneralSettings *m_generalSettings = nullptr;
  const CollectionHierarchyCache *m_hierarchyCache = nullptr;
  QLineEdit *m_searchBar = nullptr;
  QAction *m_searchModeAction = nullptr;
  QScrollArea *m_itemScrollArea = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QWidget *m_collectionPage = nullptr;
  QWidget *m_itemsPage = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  const int *m_currentCollectionIndex = nullptr;

  // Memoizes hasDirectItemsForIndex() so toggling search mode / entering a
  // collection doesn't re-stat one or many media directories on the UI thread
  // (Kartend-xkxka) — allowAllFor() probes every candidate root per toggle.
  // Keyed by collection index; the whole map is dropped on collectionsModified
  // (the same trigger that rebuilds the hierarchy cache), since that's when
  // indices and media-dir config can change.
  mutable QHash<int, bool> m_hasDirectItemsCache;
  QMetaObject::Connection m_collectionsModifiedConn;

  TimerUtils::DebouncedTimer *m_searchDebounceTimer = nullptr;
  QMetaObject::Connection m_searchItemsLoadedConn;
  // Single restartable refocus timer — replaces three uncancellable singleShots
  // that forced focus regardless of where the user had moved it (Kartend-8oau).
  QTimer *m_searchBarRefocusTimer = nullptr;

  SearchMode m_currentSearchMode = SearchMode::CurrentCollection;
  QString m_currentSearchText;
  bool m_searchActive = false;
  int m_preSearchCollectionIndex = -1;
  SearchMode m_preSearchMode = SearchMode::CurrentCollection;
  int m_preSearchSelectedIndex = -1;
  int m_preSearchTotalItems = -1;
  /// Set when search starts from the synthetic Home view (no host
  /// collection). On clear, route back to loadRootView() instead of
  /// the per-collection pre-search restore path.
  bool m_preSearchInRootView = false;

  // Adaptive debounce: tracks keystroke timing to adjust debounce delay
  qint64 m_lastKeystrokeTime = 0;
  int m_adaptiveDebounceMs = 0; // 0 = use default from UIConstants
  static constexpr int MIN_ADAPTIVE_DEBOUNCE_MS = 80;
  static constexpr int MAX_ADAPTIVE_DEBOUNCE_MS = 250;
  void updateAdaptiveDebounce();
};

#endif