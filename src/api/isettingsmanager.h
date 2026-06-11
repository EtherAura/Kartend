#ifndef ISETTINGSMANAGER_H
#define ISETTINGSMANAGER_H

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "errorutils.h"
#include <QList>
#include <QObject>

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
  virtual void loadGeneralSettings(GeneralSettings &settings) = 0;
  virtual ErrorUtils::Result<void> saveGeneralSettings(const GeneralSettings &settings) = 0;
  virtual void setLastSelectedItem(int collectionIndex, int itemIndex) = 0;
  [[nodiscard]] virtual int getLastSelectedItem(int collectionIndex) const = 0;

  /// Non-empty while at least one scraper credential sits in the INI as
  /// plaintext because a platform-keychain write failed (Kartend-ztc64).
  /// The value is the human-readable failure reason (QKeychain error string
  /// or a timeout note). Persisted across restarts via a meta key in
  /// [Scrapers] and cleared by the next save whose keychain writes all
  /// succeed — the retried write re-promotes the plaintext value and the
  /// INI copy is replaced by the @keychain sentinel. The settings dialog
  /// seeds its non-modal warning banner from this getter and tracks live
  /// changes via credentialStorageDemotionChanged().
  [[nodiscard]] virtual QString credentialDemotionReason() const = 0;

  // The settings-dialog orchestration methods (openSettingsDialog,
  // handleReloadRequired, handleLayoutChanges) moved off this interface to
  // the ui-layer SettingsDialogController (Kartend-q8p29) — the dialog flow
  // is ui orchestration, not persistence. This interface keeps the
  // load/save surface only.

signals:
  void collectionsModified();

  /// Emitted from saveGeneralSettings() after a clean disk write whenever the
  /// credential-storage demotion state changed (Kartend-ztc64). A non-empty
  /// @p reason means a keychain write just failed and the affected
  /// credential(s) were written to the INI as plaintext; an empty @p reason
  /// means a subsequent save's keychain writes all succeeded and the
  /// plaintext copies were re-promoted (or removed). The settings dialog
  /// uses this to show/hide the inline warning banner without reopening.
  void credentialStorageDemotionChanged(const QString &reason);

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
