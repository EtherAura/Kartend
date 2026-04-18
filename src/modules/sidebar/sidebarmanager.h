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
struct ApplicationContext;

struct SidebarManagerSetup {
  ApplicationContext *ctx = nullptr;

  MetadataSidebar *sidebar = nullptr;
  QWidget *itemsPage = nullptr;
  QHBoxLayout *mainLayout = nullptr;
  QScrollArea *scrollArea = nullptr;
  SettingsManager *settingsManager = nullptr;
  ArtworkManager *artworkManager = nullptr;
  QList<CollectionConfig> *collections = nullptr;

  SETUP_GETTER_DECL(MetadataSidebar *, Sidebar)
  SETUP_GETTER_DECL(QWidget *, ItemsPage)
  SETUP_GETTER_DECL(QScrollArea *, ScrollArea)
  SETUP_GETTER_DECL(SettingsManager *, SettingsManager)
  SETUP_GETTER_DECL(ArtworkManager *, ArtworkManager)
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
  [[nodiscard]] bool isSidebarVisible() const;
  void saveSidebarStateForCollection(int collectionIndex, bool visible);
  void saveSidebarStateForCollection(const QString &collectionName,
                                     bool visible);
  [[nodiscard]] int currentCollectionIndex() const {
    return m_currentCollectionIndex;
  }

signals:
  void sidebarVisibilityChanged(bool visible);
  void sidebarLayoutChanged();

private:
  MetadataSidebar *m_MetadataSidebar;
  QWidget *m_itemsPage;
  QHBoxLayout *m_mainHorizontalLayout;
  QScrollArea *m_itemScrollArea;
  SettingsManager *m_settingsManager = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  bool m_sidebarVisible = false;
  int m_currentCollectionIndex;
};

#endif