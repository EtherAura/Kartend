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
class DetailsPaneManager;
class SessionManager;
class SettingsManager;
class NavigationManager;
class AnimationManager;
class ViewportManager;
class ArtworkManager;
class DetailsPane;
class InteractionStateHolder;
class QWidget;
class QScrollArea;
class QLineEdit;
class QMouseEvent;
struct ApplicationContext;

struct SelectionManagerSetup {
  const ApplicationContext *ctx = nullptr;

  // UI / collection-state references — sibling managers are read directly from
  // ctx at runtime, never duplicated here.
  DetailsPane *sidebar = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *gridContainer = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  const CollectionHierarchyCache *hierarchyCache = nullptr;
  QLineEdit *searchBar = nullptr;

  SETUP_GETTER_DECL(DetailsPane *, Sidebar)
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
 * Coordinates with ScrollManager for widget lookups and DetailsPaneManager for
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
  // Restore-state accessors proxy the canonical state in InteractionStateHolder
  // (m_state->selectionRestore()). m_state is asserted non-null in setup, so
  // these accessors don't guard.
  [[nodiscard]] bool isRestoringSelection() const;
  [[nodiscard]] int targetRestoreIndex() const;

  // Restore state flags (exposed for InteractionManager coordination)
  void setRestoringSelection(bool restoring);
  void setTargetRestoreIndex(int index);
  void setForceImmediateCenter(bool force);
  [[nodiscard]] bool forceImmediateCenter() const;

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

  // Selection restore state lives in InteractionStateHolder (m_state) — the
  // single source of truth. Accessors above proxy through it.

  // ctx is the single source of truth for sibling managers + state. Inline
  // accessors below are the canonical read path.
  const ApplicationContext *m_ctx = nullptr;
  [[nodiscard]] InteractionStateHolder *state() const {
    return m_ctx ? m_ctx->interactionState() : nullptr;
  }
  [[nodiscard]] ScrollManager *scrollMgr() const {
    return m_ctx ? m_ctx->scrollManager() : nullptr;
  }
  [[nodiscard]] DetailsPaneManager *detailsPaneMgr() const {
    return m_ctx ? m_ctx->detailsPaneManager() : nullptr;
  }
  [[nodiscard]] SessionManager *sessionMgr() const {
    return m_ctx ? m_ctx->sessionManager() : nullptr;
  }
  [[nodiscard]] SettingsManager *settingsMgr() const {
    return m_ctx ? m_ctx->settingsManager() : nullptr;
  }
  [[nodiscard]] NavigationManager *navMgr() const {
    return m_ctx ? m_ctx->navigationManager() : nullptr;
  }
  [[nodiscard]] AnimationManager *animMgr() const {
    return m_ctx ? m_ctx->animationManager() : nullptr;
  }
  [[nodiscard]] ViewportManager *viewportMgr() const {
    return m_ctx ? m_ctx->viewportManager() : nullptr;
  }
  [[nodiscard]] ArtworkManager *artworkMgr() const {
    return m_ctx ? m_ctx->artworkManager() : nullptr;
  }

  DetailsPane *m_MetadataSidebar = nullptr;
  const CollectionHierarchyCache *m_hierarchyCache = nullptr;
  QLineEdit *m_searchBar = nullptr;
  QWidget *m_itemsPage = nullptr;
  QWidget *m_gridContainer = nullptr;
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;
};

#endif // SELECTIONMANAGER_H
