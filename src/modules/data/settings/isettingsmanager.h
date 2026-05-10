#ifndef ISETTINGSMANAGER_H
#define ISETTINGSMANAGER_H

#include "collectionutils.h"
#include <QList>
#include <QObject>

class QWidget;
class DetailsPaneManager;
class ScrollManager;
class NavigationManager;
class ArtworkManager;
class CacheManager;
class IDatabaseManager;

struct SettingsDialogContext {
  QWidget *parent = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  DetailsPaneManager *detailsPaneManager = nullptr;
  ScrollManager *scrollManager = nullptr;
  NavigationManager *navigationManager = nullptr;
  // needed so the dialog controller can subscribe to post-scan summary
  // signals and display the "X of Y items added" confirmation box when a
  // newly-added collection finishes its first scan.
  IDatabaseManager *databaseManager = nullptr;
};

/**
 * @brief Abstract interface to the settings/configuration layer.
 *
 * Exposes the public API that the rest of the app (and future test doubles)
 * binds against, separated from the QSettings-backed disk implementation.
 */
class ISettingsManager : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  ~ISettingsManager() override = default;

  virtual void loadCollections(QList<CollectionConfig> &collections) = 0;
  virtual void saveCollections(const QList<CollectionConfig> &collections) = 0;
  virtual void openSettingsDialog(const SettingsDialogContext &context) = 0;
  virtual void loadGeneralSettings(GeneralSettings &settings) = 0;
  virtual void saveGeneralSettings(const GeneralSettings &settings) = 0;
  virtual void setLastSelectedItem(int collectionIndex, int itemIndex) = 0;
  [[nodiscard]] virtual int getLastSelectedItem(int collectionIndex) const = 0;

  virtual void handleReloadRequired(
      const QList<CollectionConfig> &collections, const QList<CollectionConfig> &newCollections,
      const QList<CollectionConfig> &originalCollections, int viewingCollectionIndex,
      DetailsPaneManager *detailsPaneManager, ScrollManager *scrollManager,
      NavigationManager *navigationManager, ArtworkManager *artworkManager,
      CacheManager *cacheManager, int currentCollectionIndex) = 0;

  virtual void handleLayoutChanges(QWidget *parent, const QList<CollectionConfig> &collections,
                                   int viewingCollectionIndex, bool titleChangedForView,
                                   bool scrollbarChangedForView, bool sidebarModeChangedForView,
                                   bool gridWidthChangedForView, bool spacingChangedForView,
                                   bool alignmentChangedForView, bool fontSizeChangedForView,
                                   bool hideTitlesChangedForView,
                                   DetailsPaneManager *detailsPaneManager,
                                   ScrollManager *scrollManager, ArtworkManager *artworkManager,
                                   int currentCollectionIndex) = 0;

signals:
  void collectionsModified();
};

#endif // ISETTINGSMANAGER_H
