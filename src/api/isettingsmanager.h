#ifndef ISETTINGSMANAGER_H
#define ISETTINGSMANAGER_H

#include "collectionutils.h"
#include "errorutils.h"
#include <functional>
#include <memory>
#include <QList>
#include <QObject>

class QWidget;
class IDetailsPaneManager;
class IScrollManager;
class INavigationManager;
class IArtworkManager;
class ICacheManager;
class IDatabaseManager;
class ISettingsDialog;

struct SettingsDialogContext {
  QWidget *parent = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  IDetailsPaneManager *detailsPaneManager = nullptr;
  IScrollManager *scrollManager = nullptr;
  INavigationManager *navigationManager = nullptr;
  // needed so the dialog controller can subscribe to post-scan summary
  // signals and display the "X of Y items added" confirmation box when a
  // newly-added collection finishes its first scan.
  IDatabaseManager *databaseManager = nullptr;

  // Factory that builds the concrete settings dialog and wires its
  // collectionSaved / rescanRequired signals to the supplied callbacks.
  // Supplied by MainWindow (which legally #includes the concrete
  // SettingsDialog), so the data-layer controller never names the ui/ type:
  // it constructs the dialog through this hook and drives it via the neutral
  // ISettingsDialog interface. The returned dialog is owned by the caller.
  // The Qt signal connections must be made on the concrete type, hence they
  // live in the factory rather than in the data layer.
  std::function<std::unique_ptr<ISettingsDialog>(
      QWidget *parent, const QList<CollectionConfig> &initialCollections, int initialIndex,
      std::function<void(const QList<CollectionConfig> &)> onCollectionSaved,
      std::function<void(int)> onRescanRequired)>
      createSettingsDialog;
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
  // Returns Result<void>::success() on a clean QSettings::sync, or an
  // ErrorContext describing the FileWriteError otherwise. Dialog callers
  // surface the error via ErrorDialog and keep the dialog open; non-dialog
  // callers (timers, controllers, kart imports) discard the result — the
  // implementation still logs internally.
  virtual ErrorUtils::Result<void>
  saveCollections(const QList<CollectionConfig> &collections) = 0;
  virtual void openSettingsDialog(const SettingsDialogContext &context) = 0;
  virtual void loadGeneralSettings(GeneralSettings &settings) = 0;
  virtual ErrorUtils::Result<void> saveGeneralSettings(const GeneralSettings &settings) = 0;
  virtual void setLastSelectedItem(int collectionIndex, int itemIndex) = 0;
  [[nodiscard]] virtual int getLastSelectedItem(int collectionIndex) const = 0;

  virtual void handleReloadRequired(
      const QList<CollectionConfig> &collections, const QList<CollectionConfig> &newCollections,
      const QList<CollectionConfig> &originalCollections, int viewingCollectionIndex,
      IDetailsPaneManager *detailsPaneManager, IScrollManager *scrollManager,
      INavigationManager *navigationManager, IArtworkManager *artworkManager,
      ICacheManager *cacheManager, int currentCollectionIndex) = 0;

  virtual void handleLayoutChanges(QWidget *parent, const QList<CollectionConfig> &collections,
                                   int viewingCollectionIndex, bool titleChangedForView,
                                   bool scrollbarChangedForView, bool sidebarModeChangedForView,
                                   bool gridWidthChangedForView, bool spacingChangedForView,
                                   bool alignmentChangedForView, bool fontSizeChangedForView,
                                   bool hideTitlesChangedForView,
                                   IDetailsPaneManager *detailsPaneManager,
                                   IScrollManager *scrollManager, IArtworkManager *artworkManager,
                                   int currentCollectionIndex) = 0;

signals:
  void collectionsModified();
};

#endif // ISETTINGSMANAGER_H
