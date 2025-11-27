#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include "collectionutils.h"
#include "extensionutils.h"
#include <QObject>

class QWidget;
class QFile;
class SidebarManager;
class ScrollManager;
class NavigationManager;
class SessionManager;
class ArtworkManager;
class CacheManager;

struct SettingsDialogContext {
  QWidget *parent = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  SidebarManager *sidebarManager = nullptr;
  ScrollManager *scrollManager = nullptr;
  NavigationManager *navigationManager = nullptr;
};

class SettingsManager : public QObject {
  Q_OBJECT
public:
  explicit SettingsManager(SessionManager *sessionManager,
                           ArtworkManager *artworkManager,
                           CacheManager *cacheManager,
                           QObject *parent = nullptr);
  ~SettingsManager();

  void loadCollections(QList<CollectionConfig> &collections) const;
  void saveCollections(const QList<CollectionConfig> &collections) const;
  void setupDefaultCollections(QList<CollectionConfig> &collections);
  void openSettingsDialog(const SettingsDialogContext &context);
  auto loadGeneralSettings(GeneralSettings &settings) -> void;
  auto saveGeneralSettings(const GeneralSettings &settings) -> void;
  auto setLastSelectedItem(int collectionIndex, int itemIndex) -> void;
  auto getLastSelectedItem(int collectionIndex) const -> int;

signals:
  void collectionsModified();

public:



  auto handleReloadRequired(
      QList<CollectionConfig> &collections,
      const QList<CollectionConfig> &newCollections,
      const QList<CollectionConfig> &originalCollections,
      int viewingCollectionIndex, SidebarManager *sidebarManager,
      ScrollManager *scrollManager, NavigationManager *navigationManager,
      ArtworkManager *artworkManager, CacheManager *cacheManager,
      int currentCollectionIndex) -> void;

  auto handleLayoutChanges(
      QWidget *parent, QList<CollectionConfig> &collections,
      int viewingCollectionIndex, bool titleChangedForView,
      bool scrollbarChangedForView, bool sidebarModeChangedForView,
      bool gridWidthChangedForView, bool spacingChangedForView,
      bool alignmentChangedForView, bool fontSizeChangedForView,
      bool hideTitlesChangedForView, SidebarManager *sidebarManager,
      ScrollManager *scrollManager, ArtworkManager *artworkManager,
      int currentCollectionIndex) -> void;

private:
  SessionManager *m_sessionManager = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  CacheManager *m_cacheManager = nullptr;
  GeneralSettings m_generalSettings;

  void
  finalizeCollections(const QHash<QString, CollectionConfig> &tempCollections,
                      QList<CollectionConfig> &collections,
                      bool &needsRewrite) const;
};



#endif // SETTINGSMANAGER_H