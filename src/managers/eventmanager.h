#ifndef EVENTMANAGER_H
#define EVENTMANAGER_H

#include <QObject>
#include <QPointer>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QScrollBar;
class QScrollArea;
class QStackedWidget;
class QWidget;
class QLineEdit;
class MediaItemWidget;
class ScrollManager;
class KeyboardManager;
class MouseManager;
class AnimationManager;
class ViewportManager;
class SelectionManager;
class ArtworkManager;
class DatabaseManager;
class SidebarManager;
class MainWindow;
struct CollectionConfig;
struct GeneralSettings;

/**
 * @brief Manages event filtering and dispatching for user input events.
 *
 * Handles mouse, keyboard, and wheel events, delegating to specialized
 * managers (KeyboardManager, MouseManager, etc.) as appropriate.
 * Extracted from InteractionManager to separate event handling concerns.
 */
class EventManager : public QObject {
  Q_OBJECT

public:
  explicit EventManager(QObject *parent = nullptr);
  ~EventManager() override;

  // Event filter installation
  void installEventFilters();

  // Main event filter entry point
  bool filterEvent(QObject *obj, QEvent *event);

  // References setup
  void setScrollManager(ScrollManager *manager) { m_scrollManager = manager; }
  void setKeyboardManager(KeyboardManager *manager) { m_keyboardManager = manager; }
  void setMouseManager(MouseManager *manager) { m_mouseManager = manager; }
  void setAnimationManager(AnimationManager *manager) { m_animationManager = manager; }
  void setViewportManager(ViewportManager *manager) { m_viewportManager = manager; }
  void setSelectionManager(SelectionManager *manager) { m_selectionManager = manager; }
  void setArtworkManager(ArtworkManager *manager) { m_artworkManager = manager; }
  void setDatabaseManager(DatabaseManager *manager) { m_databaseManager = manager; }
  void setSidebarManager(SidebarManager *manager) { m_sidebarManager = manager; }
  void setMainWindow(MainWindow *mainWindow) { m_mainWindow = mainWindow; }
  void setGeneralSettings(GeneralSettings *settings) { m_generalSettings = settings; }
  void setItemScrollArea(QScrollArea *scrollArea) { m_itemScrollArea = scrollArea; }
  void setGridContainer(QWidget *container) { m_gridContainer = container; }
  void setStackedWidget(QStackedWidget *widget) { m_stackedWidget = widget; }
  void setItemsPage(QWidget *page) { m_itemsPage = page; }
  void setSearchBar(QLineEdit *searchBar) { m_searchBar = searchBar; }
  void setCollections(QList<CollectionConfig> *collections) { m_collections = collections; }
  void setCurrentCollectionIndex(int *index) { m_currentCollectionIndex = index; }

  // State accessors for coordination with InteractionManager
  [[nodiscard]] bool isRestoringSelection() const;
  void setRestoringSelection(bool restoring) { m_restoringSelection = restoring; }

signals:
  // Event signals for InteractionManager to handle
  void widgetDoubleClicked(const QString &filePath, int collectionIndex);
  void widgetClicked(MediaItemWidget *widget, const QPoint &clickPos, QMouseEvent *event);
  void clearSelectionRequested();
  void slashKeyPressed();
  void escapeKeyPressed();
  void activityDetected();
  void wheelScrollStarted();
  void wheelScrollEnded();
  void scrollbarClicked();
  void requestStopRepeat(bool suppressRecentering);

private:
  // Event handlers
  bool handleActivityEvent(QEvent *event);
  bool handleMouseButtonPress(QObject *obj, QEvent *event);
  bool handleMouseButtonRelease(QObject *obj, QEvent *event);
  bool handleWheelEvent(QObject *obj, QEvent *event);
  bool handleKeyPressEvent(QObject *obj, QEvent *event);
  bool handleKeyReleaseEvent(QObject *obj, QEvent *event);
  bool handleMouseDoubleClick(QObject *obj, QEvent *event);
  bool handleMousePress(QObject *obj, QEvent *event);

  // Helper methods
  [[nodiscard]] int getCurrentGridWidth() const;
  [[nodiscard]] QList<int> getSubcollections(int parentIndex) const;
  bool applyWheelSelectionDelta(int wheelSteps);

  // Manager references
  ScrollManager *m_scrollManager = nullptr;
  KeyboardManager *m_keyboardManager = nullptr;
  MouseManager *m_mouseManager = nullptr;
  AnimationManager *m_animationManager = nullptr;
  ViewportManager *m_viewportManager = nullptr;
  SelectionManager *m_selectionManager = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  DatabaseManager *m_databaseManager = nullptr;
  SidebarManager *m_sidebarManager = nullptr;
  MainWindow *m_mainWindow = nullptr;
  GeneralSettings *m_generalSettings = nullptr;

  // UI references
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QWidget *m_itemsPage = nullptr;
  QLineEdit *m_searchBar = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;

  // State
  bool m_restoringSelection = false;
};

#endif // EVENTMANAGER_H
