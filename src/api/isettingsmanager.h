#ifndef ISETTINGSMANAGER_H
#define ISETTINGSMANAGER_H

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "errorutils.h"
#include "isettingsdialog.h" // for SettingsPage + ISettingsDialog
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

  /// Optional caller hint for the navigation row the dialog should land on
  /// when it opens. Default leaves the dialog at its standard first row.
  /// See SettingsPage for the curated set of public targets.
  SettingsPage initialPage = SettingsPage::Default;
};

/**
 * @brief Abstract interface to the settings/configuration layer.
 *
 * Exposes the public API that the rest of the app (and future test doubles)
 * binds against, separated from the QSettings-backed disk implementation.
 */
class ISettingsManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ISettingsManager)
public:
  using QObject::QObject;
  ~ISettingsManager() override = default;

  virtual void loadCollections(QList<CollectionConfig> &collections) = 0;
  // Returns Result<void>::success() on a clean QSettings::sync, or an
  // ErrorContext describing the FileWriteError otherwise. Dialog callers
  // surface the error via ErrorDialog and keep the dialog open; non-dialog
  // callers (timers, controllers, kart imports) discard the result — the
  // implementation still logs internally.
  virtual ErrorUtils::Result<void> saveCollections(const QList<CollectionConfig> &collections) = 0;
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

  /// Emitted from saveGeneralSettings() when ScraperOptions actually
  /// changed between the previously-loaded value and the new one. Background
  /// consumers (ScraperService, BatchScraperRunner) that cache fields like
  /// mediaConcurrency / mediaThrottleMs at startup connect to this and
  /// refresh on the next quiescent point — without it, in-flight scrape
  /// batches keep using the pre-Apply config and the user's change doesn't
  /// take effect until restart or the next batch boundary.
  ///
  /// Pilot signal for the per-domain hot-reload contract; the per-collection
  /// signals below complete the rollout.
  void scraperOptionsChanged(const ScraperOptions &options);

  // Per-collection hot-reload signals: emitted from saveCollections() after
  // a clean on-disk write, but only when the named sub-struct on the
  // collection at @p collectionIndex actually changed against the
  // previously-saved snapshot. Identity is the (name, mediaDirectory) UUID
  // so a reorder of unchanged collections doesn't fire spurious diffs.
  //
  // Consumers opt in by connecting to the signals they care about — most
  // managers only need one or two (ScrollManager: gridLayoutChanged;
  // DetailsPaneManager: sidebarAppearanceChanged; ArtworkManager:
  // scraperOverridesChanged). The "added" / "removed" lifecycle is still
  // covered by the coarse collectionsModified() signal above; these new
  // signals are strictly for in-place mutations of an existing collection.
  void gridLayoutChanged(int collectionIndex, const GridLayoutPreferences &gridLayout);
  void sidebarAppearanceChanged(int collectionIndex, const SidebarAppearance &sidebar);
  void collectionBackgroundChanged(int collectionIndex, const CollectionBackground &background);
  void listViewOptionsChanged(int collectionIndex, const ListViewOptions &listView);
  void archiveOptionsChanged(int collectionIndex, const ArchiveOptions &archive);
  void folderBrowsingOptionsChanged(int collectionIndex,
                                    const FolderBrowsingOptions &folderBrowsing);
  void collectionFilterPreferencesChanged(int collectionIndex,
                                          const CollectionFilterPreferences &filter);
  void scraperOverridesChanged(int collectionIndex, const ScraperOverrides &scraperOverrides);
  void launcherProfileChanged(int collectionIndex, const LauncherProfile &launcher);
};

#endif // ISETTINGSMANAGER_H
