#ifndef SELECTIONMANAGER_H
#define SELECTIONMANAGER_H

#include "collectionutils.h"
#include <QObject>
#include <QPointer>
#include <QString>

class MediaItemWidget;
class ScrollManager;
class SidebarManager;
class SessionManager;
class SettingsManager;
class NavigationManager;
class AnimationManager;
class ViewportManager;
class ArtworkManager;
class MainWindow;
class metadataSidebar;
class QWidget;
class QScrollArea;
class QMouseEvent;

struct SelectionManagerSetup {
  ScrollManager *scrollManager = nullptr;
  SidebarManager *sidebarManager = nullptr;
  SessionManager *sessionManager = nullptr;
  SettingsManager *settingsManager = nullptr;
  NavigationManager *navigationManager = nullptr;
  AnimationManager *animationManager = nullptr;
  ViewportManager *viewportManager = nullptr;
  ArtworkManager *artworkManager = nullptr;
  MainWindow *mainWindow = nullptr;
  metadataSidebar *sidebar = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *gridContainer = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
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
  [[nodiscard]] QString selectedFilePath() const { return m_selectedFilePath; }
  [[nodiscard]] MediaItemWidget *selectedWidget() const {
    return m_selectedMediaItem;
  }

  // Selection state mutators
  void setSelectedIndex(int index);
  void setSelectedFilePath(const QString &path);
  void setSelectedWidget(MediaItemWidget *widget);

  // Primary selection operations
  void clearSelection(bool isShuttingDown = false);
  void clearSelectionAndFocus();

  // Selection restore operations
  void beginSelectionRestore(int targetIndex);
  void cancelPendingSelectionRestore();
  void prepareForRestore(int targetIndex);
  void finalizeRestore();
  [[nodiscard]] bool isRestoringSelection() const {
    return m_restoringSelection;
  }
  [[nodiscard]] int targetRestoreIndex() const { return m_targetRestoreIndex; }

  // Restore state flags (exposed for InteractionManager coordination)
  void setRestoringSelection(bool restoring) {
    m_restoringSelection = restoring;
  }
  void setTargetRestoreIndex(int index) { m_targetRestoreIndex = index; }
  void setForceImmediateCenter(bool force) { m_forceImmediateCenter = force; }
  [[nodiscard]] bool forceImmediateCenter() const {
    return m_forceImmediateCenter;
  }

  // Selection persistence
  void persistSelection(int collectionIndex, int itemIndex,
                        const QString &title);

  // Derive title for an index (used for persistence)
  [[nodiscard]] QString titleForIndex(int index,
                                       const QList<int> &subcollections) const;

  // File path resolution for selection
  void updateFilePathForSelection(int index, const QList<int> &subcollections);

  // Widget selection state updates
  void applyWidgetSelection(MediaItemWidget *widget);
  void clearWidgetSelection(MediaItemWidget *widget);

  // Lookup widget for index from ScrollManager
  [[nodiscard]] MediaItemWidget *widgetForIndex(int index) const;

  // Get subcollections list for current collection
  [[nodiscard]] QList<int> getSubcollections(int parentIndex) const;

  // Check if selection matches restore target and finalize restore
  bool checkAndFinalizeRestore(int index);

  // Click selection helpers
  [[nodiscard]] bool shouldTreatAsNewRow(int targetIndex, int gridWidth) const;
  [[nodiscard]] static bool shouldAnimateHorizontalHop(int fromIndex,
                                                        int toIndex,
                                                        int gridWidth);
  [[nodiscard]] static bool isNewRow(int currentSelection, int newSelection,
                                     int gridWidth);

  // Row tracking for click detection
  void setLastSelectedRow(int row) { m_lastSelectedRow = row; }
  [[nodiscard]] int lastSelectedRow() const { return m_lastSelectedRow; }

  // Click selection processing (moved from InteractionManager)
  void processSingleClickSelection(int visualIndex, const QString &filePath);
  void runHorizontalHopAnimation(int start, int target, qint64 nowMs);
  void handleNewRowClickSelection(int visualIndex, qint64 nowMs);
  void handleSameRowClickSelection(int visualIndex, bool skipCenter, qint64 nowMs);

  // Widget click handling
  int handleWidgetSelection(MediaItemWidget *widget, const QPoint &clickPos,
                            QMouseEvent *originalEvent);
  void handleWidgetClicked(MediaItemWidget *widget, const QString &filePath);

  // Selection restore (full implementation)
  void beginFullSelectionRestore(int targetIndex);
  void applySelectionStateForIndex(int idx);
  void finalizeRestoreFlagsAndFocus();
  void scheduleSidebarMetadataUpdateIfVisible(int targetIndex,
                                              int initialDelayMs,
                                              int secondaryDelayMs);

  // Select item by index with full update
  void selectItemByIndex(int index, bool allowHorizontalScroll);

  // Persistence helpers
  void persistSuppressedSelectionAndMaybeCenter(
      int index, const QList<int> &subcollections, bool skipCenter);
  void handleSuccessfulSelection(int index);
  [[nodiscard]] QString titleForIndexInColl(int coll, int idx) const;
  void persistSelectionForIndex(int coll, int idx);

  // Try to select widget with retry
  void trySelectWidget(int index, const QList<int> &subcollections,
                       int attempt);

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
  MediaItemWidget *m_selectedMediaItem = nullptr;
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
  ScrollManager *m_scrollManager = nullptr;
  SidebarManager *m_sidebarManager = nullptr;
  SessionManager *m_sessionManager = nullptr;
  SettingsManager *m_settingsManager = nullptr;
  NavigationManager *m_navigationManager = nullptr;
  AnimationManager *m_animationManager = nullptr;
  ViewportManager *m_viewportManager = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  MainWindow *m_mainWindow = nullptr;
  metadataSidebar *m_metadataSidebar = nullptr;
  QWidget *m_itemsPage = nullptr;
  QWidget *m_gridContainer = nullptr;
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;
};

#endif // SELECTIONMANAGER_H
