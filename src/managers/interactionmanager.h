#ifndef INTERACTIONMANAGER_H
#define INTERACTIONMANAGER_H

#include "collectionutils.h"
#include "eventmanager.h"
#include "keyboardmanager.h"
#include "launchmanager.h"
#include "mousemanager.h"
#include "animationmanager.h"
#include "searchmanager.h"
#include "selectionmanager.h"
#include "viewportmanager.h"
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QObject>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QVector>
#include <memory>

class QTimer;
class MainWindow;
class MediaItemWidget;
class DatabaseManager;
class NavigationManager;
class SettingsManager;
class SidebarManager;
class ScrollManager;
class SessionManager;
class ArtworkManager;
class metadataSidebar;

struct InteractionManagerSetup {
  ScrollManager *scrollManager = nullptr;
  SidebarManager *sidebarManager = nullptr;
  SettingsManager *settingsManager = nullptr;
  DatabaseManager *databaseManager = nullptr;
  NavigationManager *navigationManager = nullptr;
  SessionManager *sessionManager = nullptr;
  ArtworkManager *artworkManager = nullptr;
  metadataSidebar *sidebar = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *collectionPage = nullptr;
  QLineEdit *searchBar = nullptr;
  QPushButton *searchModeButton = nullptr;
  MainWindow *mainWindow = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
};

class InteractionManager : public QObject {
  Q_OBJECT
public:
  explicit InteractionManager(QObject *parent = nullptr);
  ~InteractionManager() override;
  void setupReferences(const InteractionManagerSetup &setup);
  void handleWidgetClicked(MediaItemWidget *widget, const QString &filePath);
  void handleWidgetDoubleClickedWithCollection(const QString &filePath,
                                               int collectionIndex);
  void selectItemByIndex(int index, bool allowHorizontalScroll);
  void clearSelection();
  void clearSelectionAndFocus();
  int currentSelectedIndex() const;
  int getCurrentGridWidth() const;
  void toggleSearchMode();
  void updateSearchModeButton();
  void updateSearchBarPlaceholder();
  void launchItemWithCollection(const QString &filePath, int collectionIndex);
  bool isWheelScrolling() const;
  auto eventFilter(QObject *obj, QEvent *event) -> bool override;
  auto handleGlobalKeyPress(QKeyEvent *event) -> bool;
  MediaItemWidget *getSelectedMediaItem() const;
  void setSelectedMediaItem(MediaItemWidget *widget);
  QString selectedFilePath() const;
  void ensureItemVisible(int index, bool allowHorizontalScroll);
  void initializeSearchModeForCurrentCollection();
  void beginSelectionRestore(int targetIndex);
  void cancelPendingSelectionRestore();
  void stopRepeat(bool suppressRecentering = false);
  bool isRestoringSelection() const;
  int targetRestoreIndex() const;
  bool forceImmediateCenter() const;
  void recenterCurrentSelection();

  bool m_navigationInProgress = false;

private:
  // Selection state - kept for internal use during transition, synced with SelectionManager
  bool m_restoringSelection = false;
  int m_targetRestoreIndex = -1;
  QString m_selectedFilePath;
  int m_selectedItemIndex = -1;

  int m_selectionRestoreToken = 0;
  bool m_selectionRestorePending = false;

signals:
  void selectionChanged(int index);
  void searchModeChanged(SearchMode mode);

public slots:
  void saveCurrentSelection();
  void handleImmediateSearchTextChanged(const QString &text);

private slots:
  // KeyboardManager callbacks
  void handleArrowKeyNavigation(int direction, bool vertical);
  void onKeyboardRepeatStep();
  void onKeyboardStopRepeat(bool suppressRecentering);

  // MouseManager callbacks
  void onMouseHoldScrollStep(int direction, bool isHorizontal);

private:
  QList<int> getSubcollections(int parentIndex) const;
  void updateFilePathForSelection(int index, const QList<int> &subcollections);
  void trySelectWidget(int index, const QList<int> &subcollections,
                       int attempt);
  void handleSuccessfulSelection(int index);
  void centerItemVertically(int index, bool immediate);
  void ensureHorizontallyVisible(int index);
  bool handleSlashKey();
  bool handleEscapeKey();

  int resolveDoubleClickIndexCandidate() const;
  QString derivePathFromIndex(int idx) const;
  int resolveOwnerForPath(const QString &path) const;
  int getFallbackCollectionIndex() const;

  // Search delegation (owned helper)
  std::unique_ptr<SearchManager> m_searchManager;

  // Selection delegation (owned helper)
  std::unique_ptr<SelectionManager> m_selectionManager;

  // Keyboard delegation (owned helper)
  std::unique_ptr<KeyboardManager> m_keyboardManager;

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
  MainWindow *m_mainWindow = nullptr;

  MediaItemWidget *m_selectedMediaItem = nullptr;
  SearchMode m_currentSearchMode = SearchMode::CurrentCollection;
  bool m_isShuttingDown = false;

  void scheduleScrollbarRecovery();
  QMetaObject::Connection m_scrollbarRecoveryConn;

  void applySelectionStateForIndex(int idx);
  void finalizeRestoreFlagsAndFocus();
  void scheduleSidebarMetadataUpdateIfVisible(int targetIndex,
                                              int initialDelayMs,
                                              int secondaryDelayMs);
  QString titleForIndexInColl(int coll, int idx) const;
  void persistSelectionForIndex(int coll, int idx);

  void setPendingSelectionIfNeeded(bool condition, int newSelection);
  void updateSelectionStateAfterMove(int newSelection);
  auto processEnterOrReturnKey(int totalItems) -> bool;
  auto handleEnterOnSubcollection(int currentSelection, const QList<int> &subs)
      -> bool;
  auto handleEnterOnItem(int currentSelection, int totalItems) -> bool;
  auto isItemOffscreen(int selection, int gridWidth) const -> bool;
  void applyMinorHorizontalSuppress();

  // Selection helpers
  void persistSuppressedSelectionAndMaybeCenter(
      int index, const QList<int> &subcollections, bool skipCenter);

  // Key navigation helpers still used by handleArrowKeyNavigation
  void updateSelectionForKeyMove(int newSelection);
  void performVisibilityForKeyMove(bool isNewRow, int newSelection);
};

#endif