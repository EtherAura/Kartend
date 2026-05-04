#ifndef SIDEBARMANAGER_H
#define SIDEBARMANAGER_H

#include "collectionutils.h"
#include "setuputils.h"
#include <QObject>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QScrollArea;
class MetadataSidebar;
class ItemWidget;
class SettingsManager;
class ArtworkManager;
class DatabaseManager;
struct ApplicationContext;

struct SidebarManagerSetup {
  ApplicationContext *ctx = nullptr;

  MetadataSidebar *sidebar = nullptr;
  QWidget *itemsPage = nullptr;
  QHBoxLayout *mainLayout = nullptr;
  QScrollArea *scrollArea = nullptr;
  SettingsManager *settingsManager = nullptr;
  ArtworkManager *artworkManager = nullptr;
  DatabaseManager *databaseManager = nullptr;
  QList<CollectionConfig> *collections = nullptr;

  SETUP_GETTER_DECL(MetadataSidebar *, Sidebar)
  SETUP_GETTER_DECL(QWidget *, ItemsPage)
  SETUP_GETTER_DECL(QScrollArea *, ScrollArea)
  SETUP_GETTER_DECL(SettingsManager *, SettingsManager)
  SETUP_GETTER_DECL(ArtworkManager *, ArtworkManager)
  SETUP_GETTER_DECL(DatabaseManager *, DatabaseManager)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
};

class SidebarManager : public QObject {
  Q_OBJECT

public:
  explicit SidebarManager(QObject *parent = nullptr);
  void setupReferences(const SidebarManagerSetup &setup);
  void setupSidebar();
  void toggleSidebar();
  void updateSidebarMetadata(ItemWidget *selectedItem);
  void applySidebarStateForCollection(int collectionIndex);
  void updateSidebarLayout(int currentCollectionIndex);
  void positionSidebarOverlay();
  /// Recomputes the collection-level summary the sidebar shows when no
  /// item is selected (Kartend-3mn). Cheap; safe to call after collection
  /// switches, scan completions, or settings saves.
  void refreshCollectionSummary();
  [[nodiscard]] bool isSidebarVisible() const;
  void saveSidebarStateForCollection(int collectionIndex, bool visible);
  void saveSidebarStateForCollection(const QString &collectionName, bool visible);
  [[nodiscard]] int currentCollectionIndex() const { return m_currentCollectionIndex; }

  /// Kartend-3ile: external override that hides the sidebar without touching
  /// the persisted per-collection sidebarVisible flag. Cover flow uses this
  /// to take the full viewport while preserving the user's sidebar
  /// preference for grid/list views. Setting back to false re-runs layout
  /// from the persisted state. Toggling via toggleSidebar() also clears the
  /// override so a deliberate user toggle wins.
  void setExternallyHidden(bool hidden);
  [[nodiscard]] bool isExternallyHidden() const { return m_externallyHidden; }

signals:
  void sidebarVisibilityChanged(bool visible);
  void sidebarLayoutChanged();

private slots:
  /// Opens the per-item artwork-link editor dialog for the current
  /// selection (Kartend-53vk). Persists the user's diff via DatabaseManager
  /// and refreshes the gallery so newly-set overrides appear immediately.
  void openArtworkLinksDialog();

private:
  MetadataSidebar *m_MetadataSidebar;
  QWidget *m_itemsPage;
  QHBoxLayout *m_mainHorizontalLayout;
  QScrollArea *m_itemScrollArea;
  SettingsManager *m_settingsManager = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  DatabaseManager *m_databaseManager = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  bool m_sidebarVisible = false;
  /// Kartend-3ile: separate from m_sidebarVisible because this flag is
  /// driven by the active view type (cover flow auto-hides) rather than
  /// the user's per-collection preference. Effective visibility is the
  /// AND of (!m_externallyHidden) and m_sidebarVisible.
  bool m_externallyHidden = false;
  int m_currentCollectionIndex;

  // Snapshot of the currently-displayed item used by the artwork link
  // editor (Kartend-53vk). Captured in updateSidebarMetadata so the dialog
  // doesn't have to recompute owning collection / UUID resolution.
  QString m_currentItemFilePath;
  QString m_currentItemName;
  QString m_currentItemUuid;
  int m_currentItemOwningIndex = -1;
  QString m_currentItemArtworkDir;
};

#endif