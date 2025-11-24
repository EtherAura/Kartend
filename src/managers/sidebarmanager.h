#ifndef SIDEBARMANAGER_H
#define SIDEBARMANAGER_H

#include "collectionconfig.h"
#include <QObject>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QScrollArea;
class metadataSidebar;
class MediaItemWidget;
class SettingsManager;
class ArtworkManager;

struct SidebarManagerSetup {
  metadataSidebar *sidebar = nullptr;
  QWidget *itemsPage = nullptr;
  QHBoxLayout *mainLayout = nullptr;
  QScrollArea *scrollArea = nullptr;
  SettingsManager *settingsManager = nullptr;
  ArtworkManager *artworkManager = nullptr;
  QList<CollectionConfig> *collections = nullptr;
};

class SidebarManager : public QObject {
  Q_OBJECT

public:
  explicit SidebarManager(QObject *parent = nullptr);
  void setupReferences(const SidebarManagerSetup &setup);
  void setupSidebar();
  void toggleSidebar();
  void updateSidebarMetadata(MediaItemWidget *selectedItem);
  void applySidebarStateForCollection(int collectionIndex);
  void updateSidebarLayout(int currentCollectionIndex);
  void positionSidebarOverlay();
  bool isSidebarVisible() const;
  void saveSidebarStateForCollection(int collectionIndex, bool visible);
  void saveSidebarStateForCollection(const QString &collectionName,
                                     bool visible);

signals:
  void sidebarVisibilityChanged(bool visible);
  void sidebarLayoutChanged();

private:
  metadataSidebar *m_metadataSidebar;
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