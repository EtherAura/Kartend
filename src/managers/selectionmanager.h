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
class MainWindow;
class metadataSidebar;
class QWidget;
class QScrollArea;

struct SelectionManagerSetup {
  ScrollManager *scrollManager = nullptr;
  SidebarManager *sidebarManager = nullptr;
  SessionManager *sessionManager = nullptr;
  SettingsManager *settingsManager = nullptr;
  NavigationManager *navigationManager = nullptr;
  MainWindow *mainWindow = nullptr;
  metadataSidebar *metadataSidebar = nullptr;
  QWidget *itemsPage = nullptr;
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

  // Row tracking for click detection
  void setLastSelectedRow(int row) { m_lastSelectedRow = row; }
  [[nodiscard]] int lastSelectedRow() const { return m_lastSelectedRow; }

signals:
  void selectionChanged(int index);
  void selectionCleared();
  void requestFocusItemsPage();
  void requestViewportPositioning(int targetIndex);
  void requestStopScrollAnimations();
  void requestSidebarUpdate(int targetIndex);

private:
  void clearWidgetSelectionStates();
  void clearMetadataSidebar();
  void notifyScrollManagerOfSelection(int index);

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
  MainWindow *m_mainWindow = nullptr;
  metadataSidebar *m_metadataSidebar = nullptr;
  QWidget *m_itemsPage = nullptr;
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;
};

#endif // SELECTIONMANAGER_H
