#ifndef SELECTIONMANAGER_H
#define SELECTIONMANAGER_H

#include "collectionutils.h"
#include "setuputils.h"
#include <QObject>
#include <QPointer>
#include <QScrollArea>
#include <QString>

class ItemWidget;
class ScrollManager;
class SidebarManager;
class SessionManager;
class SettingsManager;
class NavigationManager;
class AnimationManager;
class ViewportManager;
class ArtworkManager;
class MetadataSidebar;
class InteractionStateHolder;
class QWidget;
class QScrollArea;
class QLineEdit;
class QMouseEvent;
struct ApplicationContext;

struct SelectionManagerSetup {
  const ApplicationContext *ctx = nullptr;

  ScrollManager *scrollManager = nullptr;
  SidebarManager *sidebarManager = nullptr;
  SessionManager *sessionManager = nullptr;
  SettingsManager *settingsManager = nullptr;
  NavigationManager *navigationManager = nullptr;
  AnimationManager *animationManager = nullptr;
  ViewportManager *viewportManager = nullptr;
  ArtworkManager *artworkManager = nullptr;
  MetadataSidebar *sidebar = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *gridContainer = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  const CollectionHierarchyCache *hierarchyCache = nullptr;
  QLineEdit *searchBar = nullptr;

  SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder *, InteractionState)
  SETUP_GETTER_DECL(ScrollManager *, ScrollManager)
  SETUP_GETTER_DECL(SidebarManager *, SidebarManager)
  SETUP_GETTER_DECL(SessionManager *, SessionManager)
  SETUP_GETTER_DECL(SettingsManager *, SettingsManager)
  SETUP_GETTER_DECL(NavigationManager *, NavigationManager)
  SETUP_GETTER_DECL(AnimationManager *, AnimationManager)
  SETUP_GETTER_DECL(ViewportManager *, ViewportManager)
  SETUP_GETTER_DECL(ArtworkManager *, ArtworkManager)
  SETUP_GETTER_DECL(MetadataSidebar *, Sidebar)
  SETUP_GETTER_DECL(QWidget *, ItemsPage)
  SETUP_GETTER_DECL(QWidget *, GridContainer)
  SETUP_GETTER_DECL(QScrollArea *, ItemScrollArea)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
  SETUP_GETTER_DECL(int *, CurrentCollectionIndex)
  SETUP_GETTER_DECL(const CollectionHierarchyCache *, HierarchyCache)
  SETUP_GETTER_DECL(QLineEdit *, SearchBar)
};

/**
 * @brief Manages selection state for media items in the grid.
 *
 * Owns the core selection state (selected index, file path, widget pointer)
 * and provides operations for selecting, clearing, and restoring selections.
 * Coordinates with ScrollManager for widget lookups and SidebarManager for
 * metadata updates.
 */
class SelectionManager : public QObject {
  Q_OBJECT

public:
  explicit SelectionManager(QObject *parent = nullptr);
  ~SelectionManager() override;

  void setupReferences(const SelectionManagerSetup &setup);

  // Core selection state accessors
  [[nodiscard]] int currentSelectedIndex() const { return m_selectedItemIndex; }
  [[nodiscard]] const QString &selectedFilePath() const { return m_selectedFilePath; }
  [[nodiscard]] ItemWidget *selectedWidget() const { return m_selectedMediaItem; }

  // Selection state mutators
  void setSelectedIndex(int index);
  void setSelectedFilePath(const QString &path);
  void setSelectedWidget(ItemWidget *widget);

  // Emit selectionChanged for the current index. Use after calling
  // setSelectedIndex() from paths that don't go through selectItemByIndex /
  // selectItemByHover (e.g. wheel scroll, arrow handler, hold-scroll) so that
  // listeners like the toolbar position label stay in sync.
  void notifySelectionChanged();

  // Primary selection operations
  void clearSelection(bool isShuttingDown = false);
  void clearSelectionAndFocus();

  // Selection restore operations
  void beginSelectionRestore(int targetIndex);
  void cancelPendingSelectionRestore();
  void resetSelectionRestoreState(); // Reset state for new navigation (doesn't
                                     // set userSelectionMade)
  void prepareForRestore(int targetIndex);
  void finalizeRestore();
  [[nodiscard]] bool isRestoringSelection() const { return m_restoringSelection; }
  [[nodiscard]] int targetRestoreIndex() const { return m_targetRestoreIndex; }

  // Restore state flags (exposed for InteractionManager coordination)
  void setRestoringSelection(bool restoring) { m_restoringSelection = restoring; }
  void setTargetRestoreIndex(int index) { m_targetRestoreIndex = index; }
  void setForceImmediateCenter(bool force) { m_forceImmediateCenter = force; }
  [[nodiscard]] bool forceImmediateCenter() const { return m_forceImmediateCenter; }

  // Selection persistence
  void persistSelection(int collectionIndex, int itemIndex, const QString &title);

  // Derive title for an index (used for persistence)
  [[nodiscard]] QString titleForIndex(int index, const QList<int> &subcollections) const;

  // File path resolution for selection
  void updateFilePathForSelection(int index, const QList<int> &subcollections);

  // Widget selection state updates
  void applyWidgetSelection(ItemWidget *widget);
  void clearWidgetSelection(ItemWidget *widget);

  // Lookup widget for index from ScrollManager
  [[nodiscard]] ItemWidget *widgetForIndex(int index) const;

  // Get subcollections list for current collection
  [[nodiscard]] QList<int> getSubcollections(int parentIndex) const;

  // Check if selection matches restore target and finalize restore
  bool checkAndFinalizeRestore(int index);

  // Click selection helpers
  [[nodiscard]] bool shouldTreatAsNewRow(int targetIndex, int gridWidth) const;
  [[nodiscard]] static bool shouldAnimateHorizontalHop(int fromIndex, int toIndex, int gridWidth);
  [[nodiscard]] static bool isNewRow(int currentSelection, int newSelection, int gridWidth);

  // Row tracking for click detection
  void setLastSelectedRow(int row) { m_lastSelectedRow = row; }
  [[nodiscard]] int lastSelectedRow() const { return m_lastSelectedRow; }

  // Click selection processing (moved from InteractionManager)
  void processSingleClickSelection(int visualIndex, const QString &filePath);
  void runHorizontalHopAnimation(int start, int target, qint64 nowMs);
  void handleNewRowClickSelection(int visualIndex, qint64 nowMs);
  void handleSameRowClickSelection(int visualIndex, bool skipCenter, qint64 nowMs);

  // Widget click handling via EventManager
  // handleWidgetSelectionByIndex is preferred - uses pre-computed index from
  // click detection
  int handleWidgetSelectionByIndex(int visualIndex, const QPoint &clickPos,
                                   QMouseEvent *originalEvent);
  // Legacy method - searches for widget in activeWidgets (may fail during rapid
  // scrolling)
  int handleWidgetSelection(ItemWidget *widget, const QPoint &clickPos, QMouseEvent *originalEvent);

  // Selection restore (full implementation)
  void beginFullSelectionRestore(int targetIndex);
  void applySelectionStateForIndex(int idx);
  void finalizeRestoreFlagsAndFocus();
  void scheduleSidebarMetadataUpdateIfVisible(int targetIndex, int initialDelayMs,
                                              int secondaryDelayMs);

  // Select item by index with full update
  void selectItemByIndex(int index, bool allowHorizontalScroll);
  // Select the item currently under the mouse cursor without recentering the
  // viewport; hover selection should not move content out from under the
  // pointer.
  void selectItemByHover(int index);

  // Persistence helpers
  void persistSuppressedSelectionAndMaybeCenter(int index, const QList<int> &subcollections,
                                                bool skipCenter);
  void handleSuccessfulSelection(int index);
  [[nodiscard]] QString titleForIndexInColl(int coll, int idx) const;
  void persistSelectionForIndex(int coll, int idx);

  // Try to select widget with retry
  void trySelectWidget(int index, const QList<int> &subcollections, int attempt);

signals:
  void selectionChanged(int index);
  void selectionCleared();
  void requestFocusItemsPage();
  void requestViewportPositioning(int targetIndex);
  void requestStopScrollAnimations();
  void requestSidebarUpdate(int targetIndex);
  void requestCenterVertically(int index, bool immediate);
  void requestEnsureHorizontallyVisible(int index);
  void requestStopRepeat();

private:
  void clearWidgetSelectionStates();
  void clearMetadataSidebar();
  void notifyScrollManagerOfSelection(int index);
  [[nodiscard]] int getCurrentGridWidth() const;

  // Core selection state
  ItemWidget *m_selectedMediaItem = nullptr;
  QString m_selectedFilePath;
  int m_selectedItemIndex = -1;
  int m_lastSelectedRow = -1;

  // Selection restore state
  bool m_restoringSelection = false;
  int m_targetRestoreIndex = -1;
  bool m_forceImmediateCenter = false;
  int m_selectionRestoreToken = 0;
  bool m_selectionRestorePending = false;

  // Manager references
  InteractionStateHolder *m_state = nullptr;
  ScrollManager *m_scrollManager = nullptr;
  SidebarManager *m_sidebarManager = nullptr;
  SessionManager *m_sessionManager = nullptr;
  SettingsManager *m_settingsManager = nullptr;
  NavigationManager *m_navigationManager = nullptr;
  AnimationManager *m_animationManager = nullptr;
  ViewportManager *m_viewportManager = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  MetadataSidebar *m_MetadataSidebar = nullptr;
  const CollectionHierarchyCache *m_hierarchyCache = nullptr;
  QLineEdit *m_searchBar = nullptr;
  QWidget *m_itemsPage = nullptr;
  QWidget *m_gridContainer = nullptr;
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;
};

#endif // SELECTIONMANAGER_H
