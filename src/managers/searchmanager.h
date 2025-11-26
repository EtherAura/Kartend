#ifndef SEARCHMANAGER_H
#define SEARCHMANAGER_H

#include "collectionutils.h"
#include "searchtypes.h"
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QTimer>

class DatabaseManager;
class NavigationManager;
class ScrollManager;
class MainWindow;

struct SearchManagerSetup {
  DatabaseManager *databaseManager = nullptr;
  NavigationManager *navigationManager = nullptr;
  ScrollManager *scrollManager = nullptr;
  MainWindow *mainWindow = nullptr;
  QLineEdit *searchBar = nullptr;
  QPushButton *searchModeButton = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
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

  // Helpers
  SearchContext computeSearchContext() const;
  QVector<SearchMode> buildSearchModeCycle(const SearchContext &ctx) const;
  bool hasDirectItemsForIndex(int idx) const;
  bool allowAllFor(const CollectionConfig &cfg, int collIndex,
                   bool hasSubs) const;

signals:
  void searchModeChanged(SearchMode mode);

private:
  DatabaseManager *m_databaseManager = nullptr;
  NavigationManager *m_navigationManager = nullptr;
  ScrollManager *m_scrollManager = nullptr;
  MainWindow *m_mainWindow = nullptr;
  QLineEdit *m_searchBar = nullptr;
  QPushButton *m_searchModeButton = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;

  SearchMode m_currentSearchMode = SearchMode::CurrentCollection;
  QString m_currentSearchText;
  bool m_searchActive = false;
  int m_preSearchCollectionIndex = -1;
  SearchMode m_preSearchMode = SearchMode::CurrentCollection;
  int m_preSearchSelectedIndex = -1;
};

#endif
