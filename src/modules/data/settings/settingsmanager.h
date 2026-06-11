#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include "isettingsmanager.h"
#include <QHash>
#include <QLoggingCategory>
#include <QPointer>
#include <QString>

class QFile;
class QSettings;
class SessionManager;
#include "applicationcontext_fwd.h"

// Defined in settingsmanager.cpp. Declared here so settingsmanagercollections.cpp
// (which implements saveCollections) can log to the same category.
Q_DECLARE_LOGGING_CATEGORY(lcSettingsManager)

class SettingsManager : public ISettingsManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SettingsManager)
public:
  explicit SettingsManager(const ApplicationContext *ctx, QObject *parent = nullptr);
  ~SettingsManager() override;

  void loadCollections(QList<CollectionConfig> &collections) override;
  // emits collectionsModified so observers (toolbar type filter, hierarchy
  // cache, sidebar summary) refresh after any save — not just
  // settings-dialog-driven ones. Non-const for that reason; the disk write
  // itself doesn't mutate SettingsManager state.
  ErrorUtils::Result<void> saveCollections(const QList<CollectionConfig> &collections) override;
  void openSettingsDialog(const SettingsDialogContext &context) override;
  void loadGeneralSettings(GeneralSettings &settings) override;
  ErrorUtils::Result<void> saveGeneralSettings(const GeneralSettings &settings) override;
  void setLastSelectedItem(int collectionIndex, int itemIndex) override;
  [[nodiscard]] int getLastSelectedItem(int collectionIndex) const override;
  [[nodiscard]] QString credentialDemotionReason() const override {
    return m_credentialDemotionReason;
  }

  void handleReloadRequired(const QList<CollectionConfig> &collections,
                            const QList<CollectionConfig> &newCollections,
                            const QList<CollectionConfig> &originalCollections,
                            int viewingCollectionIndex, IDetailsPaneManager *detailsPaneManager,
                            IScrollManager *scrollManager, INavigationManager *navigationManager,
                            IArtworkManager *artworkManager, ICacheManager *cacheManager,
                            int currentCollectionIndex) override;

  void handleLayoutChanges(QWidget *parent, const QList<CollectionConfig> &collections,
                           int viewingCollectionIndex, bool titleChangedForView,
                           bool scrollbarChangedForView, bool sidebarModeChangedForView,
                           bool gridWidthChangedForView, bool spacingChangedForView,
                           bool alignmentChangedForView, bool fontSizeChangedForView,
                           bool hideTitlesChangedForView, IDetailsPaneManager *detailsPaneManager,
                           IScrollManager *scrollManager, IArtworkManager *artworkManager,
                           int currentCollectionIndex) override;

private slots:
  /// Handles QueryManager's post-scan summary (forwarded via DatabaseManager).
  /// If the scan's UUID matches a collection the user just added through the
  /// settings dialog, pops a "Collection Added — X of Y items" message box.

  void onCollectionScanSummary(const QString &collectionUuid, int itemsScanned, int itemsApplied,
                               bool success);

private:
  // ctx is the single source of truth for sibling managers (SessionManager,
  // ArtworkManager, CacheManager).
  const ApplicationContext *m_ctx = nullptr;
  GeneralSettings m_generalSettings;

  // Kartend-ztc64: mirror of the [Scrapers]/credentialDemotionReason meta key.
  // Non-empty while a failed keychain write left plaintext credential(s) in
  // the INI. Loaded in loadScraperSection, recomputed on every
  // saveScraperSection (each save retries the keychain writes, so a healthy
  // keychain self-heals the demotion and clears this). The change signal is
  // emitted from saveGeneralSettings after a clean sync, matching the
  // per-domain hot-reload pattern.
  QString m_credentialDemotionReason;

  // UUIDs of collections the user just added through the settings
  // dialog that are still waiting for their first scan-summary signal. Value is
  // the collection's display name so the message box can reference it.
  QHash<QString, QString> m_pendingAddSummaries;
  // Parent widget for the confirmation message box. QPointer so we don't
  // outlive it if the dialog is destroyed before the async scan finishes.
  QPointer<QWidget> m_pendingAddSummaryParent;
  // Kartend-sqoq0: owner-supplied stock-modal runners captured from the
  // SettingsDialogContext at openSettingsDialog time, used by the async
  // scan-summary message boxes. Null runners fall back to direct
  // QMessageBox construction (the pre-sqoq0 behavior).
  DialogRunners m_dialogRunners;

  // Last successfully-saved collection list, used as the diff baseline for
  // the per-domain *Changed signals emitted from saveCollections(). Updated
  // atomically at the end of a successful save (and seeded from
  // loadCollections so the first save after launch has a sensible baseline
  // instead of an empty one that fires every signal at once).
  QList<CollectionConfig> m_lastSavedCollections;

  void finalizeCollections(const QHash<QString, CollectionConfig> &tempCollections,
                           QList<CollectionConfig> &collections, const bool &needsRewrite);

  // Per-collection hot-reload diff. Compares `collections` against the
  // `previous` snapshot by (name, mediaDirectory) UUID and fires the
  // per-leaf-struct *Changed signals. Called from saveCollections() after
  // a successful disk write so observers (background painter, sidebar
  // appearance, etc.) refresh only when their slice actually changed.
  // `previous` is passed explicitly (rather than read from
  // m_lastSavedCollections) because the baseline is committed *before* this
  // emit — see saveCollections() for why the ordering matters.
  void emitPerCollectionDiffs(const QList<CollectionConfig> &previous,
                              const QList<CollectionConfig> &collections);

  // Stamped into [General/schemaVersion] on save and checked on load (older
  // builds wrote it into [ScraperOptions]; load tolerates both). A higher value
  // than this build understands triggers a warn-on-load; a lower value drives
  // the migration dispatcher. See settingsmigrations.*.
  static constexpr int kSettingsSchemaVersion = 1;

  // Path-security filter applied to hand-editable path fields on load (shared
  // by the startup + launcher-preset loaders). Returns "" for an insecure
  // value so a poisoned config can't feed shell metacharacters to QProcess.
  static QString sanitizeLoadedPath(const QString &value, const QString &fieldName);

  // ── Per-section general-settings I/O ──────────────────────────────────
  // loadGeneralSettings / saveGeneralSettings are thin orchestration shells;
  // each leaf section's persistence call (plus its load-side coercions and
  // save-side clamps) lives in settingsmanager_<section>.cpp. The
  // [General]-group loaders/savers run with the caller already inside
  // keys::kGroupGeneral; the launcher-preset and scraper helpers manage their
  // own groups. save* helpers copy settings.<section> into the m_generalSettings
  // cache, re-apply the defensive clamps, then persist the cached value.
  void loadInputSection(QSettings &s, GeneralSettings &settings);
  void saveInputSection(QSettings &s, const GeneralSettings &settings);
  void loadKeybindingsSection(QSettings &s, GeneralSettings &settings);
  void saveKeybindingsSection(QSettings &s, const GeneralSettings &settings);
  void loadGamepadSection(QSettings &s, GeneralSettings &settings);
  void saveGamepadSection(QSettings &s, const GeneralSettings &settings);
  void loadViewSection(QSettings &s, GeneralSettings &settings);
  void saveViewSection(QSettings &s, const GeneralSettings &settings);
  void loadAppearanceSection(QSettings &s, GeneralSettings &settings);
  void saveAppearanceSection(QSettings &s, const GeneralSettings &settings);
  void loadStartupSection(QSettings &s, GeneralSettings &settings);
  void saveStartupSection(QSettings &s, const GeneralSettings &settings);
  void loadMediaSection(QSettings &s, GeneralSettings &settings);
  void saveMediaSection(QSettings &s, const GeneralSettings &settings);
  void loadHistorySection(QSettings &s, GeneralSettings &settings);
  void saveHistorySection(QSettings &s, const GeneralSettings &settings);
  void loadAttractSection(QSettings &s, GeneralSettings &settings);
  void saveAttractSection(QSettings &s, const GeneralSettings &settings);
  void loadMarqueeSection(QSettings &s, GeneralSettings &settings);
  void saveMarqueeSection(QSettings &s, const GeneralSettings &settings);
  void loadSplashSection(QSettings &s, GeneralSettings &settings);
  void saveSplashSection(QSettings &s, const GeneralSettings &settings);
  void loadRuntimeDetectionSection(QSettings &s, GeneralSettings &settings);
  void saveRuntimeDetectionSection(QSettings &s, const GeneralSettings &settings);
  void loadToolbarSection(QSettings &s, GeneralSettings &settings);
  void saveToolbarSection(QSettings &s, const GeneralSettings &settings);
  // launcher scalars (retroarchConfigPath) ride in [General]; the preset array
  // lives top-level, so the two halves are wired separately around endGroup().
  void loadLaunchersScalars(QSettings &s, GeneralSettings &settings);
  void saveLaunchersScalars(QSettings &s, const GeneralSettings &settings);
  void loadLaunchersPresets(QSettings &s, GeneralSettings &settings);
  void saveLaunchersPresets(QSettings &s);
  // Scraper credentials ([Scrapers], keychain-aware) + options
  // ([ScraperOptions], where the schema sentinel is stamped last).
  void loadScraperSection(QSettings &s, GeneralSettings &settings);
  void saveScraperSection(QSettings &s, const GeneralSettings &settings);
};

#endif // SETTINGSMANAGER_H
