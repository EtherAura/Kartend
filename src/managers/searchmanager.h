#ifndef SEARCHMANAGER_H
#define SEARCHMANAGER_H

#include "collectionutils.h"
#include "searchutils.h"
#include "timerutils.h"
#include <QLineEdit>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>

class DatabaseManager;
class NavigationManager;
class ScrollManager;
class SettingsManager;

struct SearchManagerSetup {
  DatabaseManager *databaseManager = nullptr;
  NavigationManager *navigationManager = nullptr;
  ScrollManager *scrollManager = nullptr;
  SettingsManager *settingsManager = nullptr;
  QLineEdit *searchBar = nullptr;
  QPushButton *searchModeButton = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *collectionPage = nullptr;
  QWidget *itemsPage = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  const CollectionHierarchyCache *hierarchyCache = nullptr;
};

class SearchManager : public QObject {
  Q_OBJECT
public:
  explicit SearchManager(QObject *parent = nullptr);
  ~SearchManager() override;

  void setupReferences(const SearchManagerSetup &setup);

  // Mode management
  SearchMode currentMode() const { return m_currentSearchMode; }
  void setCurrentMode(SearchMode mode) { m_currentSearchMode = mode; }
  void toggleSearchMode();
  void updateSearchModeButton();
  void updateSearchBarPlaceholder();
  void initializeSearchModeForCurrentCollection();

  // Search text handling
  void onSearchTextChanged(const QString &text, int currentSelectedIndex);
  void performDebouncedSearch();

  // Search state
  bool isSearchActive() const { return m_searchActive; }
  void setSearchActive(bool active) { m_searchActive = active; }
  QString currentSearchText() const { return m_currentSearchText; }
  void setCurrentSearchText(const QString &text) { m_currentSearchText = text; }

  // Pre-search state preservation
  int preSearchCollectionIndex() const { return m_preSearchCollectionIndex; }
  void setPreSearchCollectionIndex(int idx) { m_preSearchCollectionIndex = idx; }
  SearchMode preSearchMode() const { return m_preSearchMode; }
  void setPreSearchMode(SearchMode mode) { m_preSearchMode = mode; }
  int preSearchSelectedIndex() const { return m_preSearchSelectedIndex; }
  void setPreSearchSelectedIndex(int idx) { m_preSearchSelectedIndex = idx; }

  // Timer access for InteractionManager
  TimerUtils::DebouncedTimer *debounceTimer() const { return m_searchDebounceTimer; }
  QMetaObject::Connection &itemsLoadedConnection() { return m_searchItemsLoadedConn; }

  // Helpers
  SearchContext computeSearchContext() const;
  QVector<SearchMode> buildSearchModeCycle(const SearchContext &ctx) const;
  bool hasDirectItemsForIndex(int idx) const;
  bool allowAllFor(const CollectionConfig &cfg, int collIndex,
                   bool hasSubs) const;

signals:
  void searchModeChanged(SearchMode mode);
  void requestClearSelection();
  void requestSelectionRestore(int index);
  void requestScrollbarRecovery();

private:
  void scheduleSearchBarRefocusIfNeeded();

  DatabaseManager *m_databaseManager = nullptr;
  NavigationManager *m_navigationManager = nullptr;
  ScrollManager *m_scrollManager = nullptr;
  SettingsManager *m_settingsManager = nullptr;
  const CollectionHierarchyCache *m_hierarchyCache = nullptr;
  QLineEdit *m_searchBar = nullptr;
  QPushButton *m_searchModeButton = nullptr;
  QScrollArea *m_itemScrollArea = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QWidget *m_collectionPage = nullptr;
  QWidget *m_itemsPage = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;

  TimerUtils::DebouncedTimer *m_searchDebounceTimer = nullptr;
  QMetaObject::Connection m_searchItemsLoadedConn;

  SearchMode m_currentSearchMode = SearchMode::CurrentCollection;
  QString m_currentSearchText;
  bool m_searchActive = false;
  int m_preSearchCollectionIndex = -1;
  SearchMode m_preSearchMode = SearchMode::CurrentCollection;
  int m_preSearchSelectedIndex = -1;
};

#endif