#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include "collectiondifffingerprint.h"
#include "isettingsmanager.h"
#include <QHash>
#include <QLoggingCategory>
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
  [[nodiscard]] QStringList lastCollectionUuidCollisions() const override {
    return m_collectionUuidCollisions;
  }
  // emits collectionsModified so observers (toolbar type filter, hierarchy
  // cache, sidebar summary) refresh after any save — not just
  // settings-dialog-driven ones. Non-const for that reason; the disk write
  // itself doesn't mutate SettingsManager state.
  ErrorUtils::Result<void> saveCollections(const QList<CollectionConfig> &collections) override;
  void loadGeneralSettings(GeneralSettings &settings) override;
  ErrorUtils::Result<void> saveGeneralSettings(const GeneralSettings &settings) override;
  void setLastSelectedItem(int collectionIndex, int itemIndex) override;
  [[nodiscard]] int getLastSelectedItem(int collectionIndex) const override;
  [[nodiscard]] QString credentialDemotionReason() const override {
    return m_credentialDemotionReason;
  }

  /// Whether this BUILD has a keychain backend compiled in
  /// (KARTEND_HAVE_QTKEYCHAIN). Says nothing about whether the backend is
  /// currently reachable — a running keychain can still refuse a write, which
  /// is the separate runtime-demotion case.
  ///
  /// Exposed because the definition is PRIVATE to kartend_data, so nothing
  /// outside it — tests included — can answer the question by preprocessor.
  /// That opacity is part of why the non-keychain build's permanently-hidden
  /// warning banner went unnoticed: the configuration could not be asserted
  /// about (Kartend-4ahok).
  [[nodiscard]] static bool keychainBackendCompiledIn();

  // The settings-dialog orchestration half of this class (openSettingsDialog,
  // handleReloadRequired, handleLayoutChanges, onCollectionScanSummary plus
  // the pending-add-summary state) moved to the ui-layer
  // SettingsDialogController in Kartend-q8p29; this class keeps the
  // QSettings-backed persistence surface only.

private:
  // ctx is the single source of truth for sibling managers (SessionManager,
  // ArtworkManager, CacheManager).
  const ApplicationContext *m_ctx = nullptr;
  GeneralSettings m_generalSettings;

  // UUID collisions captured by the last loadCollections() so the GUI startup
  // path can surface them in a modal warning instead of only logging them
  // (Kartend audit cj462). Excludes missing-path / other non-fatal errors.
  QStringList m_collectionUuidCollisions;

  // Kartend-ztc64: mirror of the [Scrapers]/credentialDemotionReason meta key.
  // Non-empty while a failed keychain write left plaintext credential(s) in
  // the INI. Loaded in loadScraperSection, recomputed on every
  // saveScraperSection (each save retries the keychain writes, so a healthy
  // keychain self-heals the demotion and clears this). The change signal is
  // emitted from saveGeneralSettings after a clean sync, matching the
  // per-domain hot-reload pattern.
  QString m_credentialDemotionReason;

  // Per-collection fingerprint of the last successfully-saved list, keyed by
  // (name, mediaDirectory) UUID. Diff baseline for the per-domain *Changed
  // signals emitted from saveCollections(). Updated atomically at the end of a
  // successful save (and seeded from loadCollections so the first save after
  // launch has a sensible baseline instead of an empty one that fires every
  // signal at once). Kartend-lc58a: stores cluster hashes, not full
  // CollectionConfig copies, so a save no longer deep-copies every embedded
  // QHash/QList — see collectiondifffingerprint.h.
  QHash<QString, CollectionDiffFingerprint> m_lastSavedCollectionFingerprints;

  void finalizeCollections(const QHash<QString, CollectionConfig> &tempCollections,
                           QList<CollectionConfig> &collections, const bool &needsRewrite);

  // Per-collection hot-reload diff. Compares `collections` against the
  // `previous` fingerprint baseline by (name, mediaDirectory) UUID and fires the
  // per-leaf-struct *Changed signals. Called from saveCollections() after
  // a successful disk write so observers (background painter, sidebar
  // appearance, etc.) refresh only when their slice actually changed.
  // `previous` is passed explicitly (rather than read from
  // m_lastSavedCollectionFingerprints) because the baseline is committed
  // *before* this emit — see saveCollections() for why the ordering matters.
  void emitPerCollectionDiffs(const QHash<QString, CollectionDiffFingerprint> &previous,
                              const QList<CollectionConfig> &collections);

  // Stamped into [General/schemaVersion] on save and checked on load (older
  // builds wrote it into [ScraperOptions]; load tolerates both). A higher value
  // than this build understands triggers a warn-on-load; a lower value drives
  // the migration dispatcher. See settingsmigrations.*.
  static constexpr int kSettingsSchemaVersion = 4;

  // Drives the migration dispatcher for @p s when its stored schemaVersion is
  // behind this build, stamping the reached version and fsyncing on change.
  // Called from BOTH loadGeneralSettings and loadCollections: startup loads
  // collections FIRST, so gating only the general-settings path would hand
  // the first post-upgrade boot un-migrated collection sections
  // (Kartend-ob1c9 sidebar-defaults migration was the first to hit this).
  void migrateSettingsFileIfNeeded(QSettings &s, const QString &configPath, const QString &origin);

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
