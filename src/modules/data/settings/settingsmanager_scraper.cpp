// Scraper-section settings I/O. Split from settingsmanager.cpp along leaf-struct
// boundaries. Unlike the [General]-group sections, this helper manages its own
// groups: credentials live in [Scrapers] (keychain-aware) and performance/
// behavior options in [ScraperOptions], where the schema sentinel is stamped
// last. The synchronous QKeychain wrappers live here too, since this is their
// only caller.
#include "settingsmanager.h"

#include <QSet>
#include <QSettings>
#include <QStringList>

#include "collection/generalsettings.h"
#include "collection/scraper_settings_persistence.h"
#include "settingskeys.h"

#ifdef KARTEND_HAVE_QTKEYCHAIN
#include <QEventLoop>
#include <qt6keychain/keychain.h>
#include <QTimer>
#endif

namespace keys = kartend::settings::keys;

namespace {
#ifdef KARTEND_HAVE_QTKEYCHAIN
// Sentinel value stored in QSettings [Scrapers/<provider>/<field>] when the
// real credential lives in the platform keychain. On load, finding this
// sentinel triggers a keychain lookup; anything else is treated as either an
// empty/missing credential or a legacy plaintext value awaiting migration on
// the next save.
constexpr const char *kKeychainSentinel = "@keychain";
constexpr const char *kKeychainService = "io.github.EtherAura.Kartend.scrapers";

// Synchronous wrappers around QKeychain's async Job API. Credentials are
// read/written only at settings load (once at startup) and save (when the user
// clicks Save in the Settings dialog), never on a hot path, so blocking the
// calling thread on a local QEventLoop is acceptable. The insecureFallback flag
// is left at its default (false) so a missing secret service surfaces as
// NoBackendAvailable rather than silently dropping a plaintext copy into
// QSettings — settingsmanager handles the fallback explicitly so the caller can
// see the boundary and so the migration logic isn't confused by QKeychain
// doubling up its own plaintext copy.
// Cap nested QEventLoop::exec() so an unresponsive secret service daemon can't
// wedge the GUI thread. 5s is generous for a local keychain RPC; the
// startup/save callers degrade to a "keychain unavailable" failure path on
// timeout.
constexpr int kKeychainTimeoutMs = 5000;

// Runs the QKeychain job to completion with a bounded event loop. Returns true
// if the job finished within the timeout, false if the timer fired first.
// Either way the caller still inspects job.error() — on a real timeout we
// synthesize a warning so users see the daemon stall.
bool runKeychainJobBounded(QKeychain::Job &job, const char *opName) {
  QEventLoop loop;
  bool finished = false;
  QObject::connect(&job, &QKeychain::Job::finished, &loop, [&loop, &finished]() {
    finished = true;
    loop.quit();
  });
  job.start();
  // Watchdog: QKeychain has no built-in timeout. If the platform secret service
  // daemon stalls (GNOME Keyring on first-unlock prompt, KWallet not running,
  // etc.), this timer fires and quits the bounded loop so the caller falls back
  // to plaintext storage instead of wedging forever.
  QTimer::singleShot(kKeychainTimeoutMs, &loop, &QEventLoop::quit);
  loop.exec();
  if (!finished) {
    qCWarning(lcSettingsManager) << "SettingsManager: keychain" << opName << "timed out after"
                                 << kKeychainTimeoutMs << "ms";
  }
  return finished;
}

QString syncReadKeychain(const QString &key, bool *ok) {
  const QString service = QLatin1String(kKeychainService);
  QKeychain::ReadPasswordJob job(service);
  job.setAutoDelete(false);
  job.setKey(key);
  if (!runKeychainJobBounded(job, "read")) {
    if (ok) *ok = false;
    return {};
  }
  if (job.error() == QKeychain::NoError) {
    if (ok) *ok = true;
    return job.textData();
  }
  if (ok) *ok = false;
  return {};
}

// On failure, errorOut (when non-null) receives a human-readable reason —
// QKeychain's own error string, or a timeout note when the daemon stalled.
// The reason feeds the credential-demotion banner (Kartend-ztc64), so it is
// user-facing text, not a log-only detail.
bool syncWriteKeychain(const QString &key, const QString &value, QString *errorOut = nullptr) {
  const QString service = QLatin1String(kKeychainService);
  QKeychain::WritePasswordJob job(service);
  job.setAutoDelete(false);
  job.setKey(key);
  job.setTextData(value);
  if (!runKeychainJobBounded(job, "write")) {
    if (errorOut) {
      *errorOut = QStringLiteral("keychain did not respond within %1 ms").arg(kKeychainTimeoutMs);
    }
    return false;
  }
  if (job.error() == QKeychain::NoError) {
    return true;
  }
  if (errorOut) {
    *errorOut = job.errorString().isEmpty() ? QStringLiteral("keychain error %1").arg(job.error())
                                            : job.errorString();
  }
  return false;
}

bool syncDeleteKeychain(const QString &key) {
  const QString service = QLatin1String(kKeychainService);
  QKeychain::DeletePasswordJob job(service);
  job.setAutoDelete(false);
  job.setKey(key);
  if (!runKeychainJobBounded(job, "delete")) {
    return false;
  }
  // EntryNotFound on a delete is expected (key was already gone) — treat as
  // success so a re-save without the key doesn't log a warning.
  return job.error() == QKeychain::NoError || job.error() == QKeychain::EntryNotFound;
}
#endif // KARTEND_HAVE_QTKEYCHAIN
} // namespace

// Outside the anonymous namespace AND outside every #ifdef: this is the one
// place that answers "was a keychain backend compiled in?", and it has to
// compile — and give the right answer — in both configurations.
bool SettingsManager::keychainBackendCompiledIn() {
#ifdef KARTEND_HAVE_QTKEYCHAIN
  return true;
#else
  return false;
#endif
}

void SettingsManager::loadScraperSection(QSettings &s, GeneralSettings &settings) {
  // Scraper credentials live in their own [Scrapers] group with nested keys of
  // the shape <provider>/<field>=<value> (QSettings' built-in key hierarchy
  // handles the slash). Provider implementations read these via the
  // scraper.credentials map; missing entries mean "not configured" and the
  // provider should surface a friendly error rather than fall back to bundled
  // credentials.
  //
  // When KARTEND_HAVE_QTKEYCHAIN is defined, the INI value @keychain is a
  // sentinel meaning the real credential lives in the platform secret service;
  // any other non-empty value is either a legacy plaintext credential (migrated
  // to the keychain on next save) or came from a build without keychain support.
  settings.scraper.credentials.clear();
  s.beginGroup(keys::kGroupScrapers);
  // Kartend-ztc64: meta key persisted by saveScraperSection when a keychain
  // write failed and a credential was demoted to plaintext. Loaded into the
  // member (not GeneralSettings — it's manager state, not a user preference)
  // so the settings dialog can seed its warning banner across restarts.
  // Having no '/' it is skipped by the provider/field walk below.
  m_credentialDemotionReason = s.value(keys::kCredentialDemotionReason).toString();
  for (const QString &fullKey : s.allKeys()) {
    const int slash = fullKey.indexOf('/');
    if (slash <= 0 || slash >= fullKey.size() - 1) {
      // Malformed key (no provider prefix or empty field name) — skip rather
      // than poison the credential map. The credentialDemotionReason meta key
      // intentionally lands here too.
      continue;
    }
    const QString providerId = fullKey.left(slash);
    const QString fieldName = fullKey.mid(slash + 1);
    QString resolvedValue = s.value(fullKey).toString();
#ifdef KARTEND_HAVE_QTKEYCHAIN
    if (resolvedValue == QLatin1String(kKeychainSentinel)) {
      bool ok = false;
      const QString fromKeychain = syncReadKeychain(fullKey, &ok);
      if (ok) {
        resolvedValue = fromKeychain;
      } else {
        // Keychain backend dropped or the entry was wiped externally — surface
        // as a missing credential rather than handing the sentinel back to the
        // provider as if it were a real password.
        qCWarning(lcSettingsManager)
            << "Scraper credential" << fullKey << "marked @keychain but lookup failed; "
            << "treating as missing. Re-enter in Settings → Scrapers to repopulate.";
        resolvedValue.clear();
      }
    }
#endif
    settings.scraper.credentials[providerId][fieldName] = resolvedValue;
  }
  s.endGroup();

  // Scraper performance + behavior options. Live in a sibling [ScraperOptions]
  // group rather than under [Scrapers] so the credential key-walk above doesn't
  // pick them up as malformed provider/field pairs. loadOptions reads + clamps
  // the whole group; schemaVersion (read in [General] by the shell) is untouched
  // here.
  s.beginGroup(keys::kGroupScraperOptions);
  ScraperSettingsPersistence::loadOptions(s, settings.scraper.options);
  s.endGroup();
}

void SettingsManager::saveScraperSection(QSettings &s, const GeneralSettings &settings) {
  // Copy the scraper section into the cache and re-apply the option clamps so
  // getGeneralSettings() never returns an out-of-range value (the cache is the
  // clamp authority; the persistence save() writes raw).
  m_generalSettings.scraper = settings.scraper;
  m_generalSettings.scraper.options.mediaMaxDimension =
      qBound(0, m_generalSettings.scraper.options.mediaMaxDimension, 8192);
  m_generalSettings.scraper.options.mediaConcurrency =
      qBound(1, m_generalSettings.scraper.options.mediaConcurrency, 16);
  m_generalSettings.scraper.options.mediaThrottleMs =
      qBound(0, m_generalSettings.scraper.options.mediaThrottleMs, 5000);
  m_generalSettings.scraper.options.batchItemConcurrency =
      qBound(1, m_generalSettings.scraper.options.batchItemConcurrency, 16);
  m_generalSettings.scraper.options.skipRecentScrapeDays =
      qBound(0, m_generalSettings.scraper.options.skipRecentScrapeDays, 365);
  m_generalSettings.scraper.options.maxHashableSizeMB =
      qBound(1, m_generalSettings.scraper.options.maxHashableSizeMB, 65536);

  // Persist scraper credentials. Wipe the entire [Scrapers] group first so
  // removing a credential field via the UI actually clears the row from disk
  // (otherwise the next load would resurrect it).
  //
  // With KARTEND_HAVE_QTKEYCHAIN, credential values go to the platform secret
  // service and the INI holds only the @keychain sentinel as a presence marker.
  // When the keychain backend is unavailable (headless Linux without dbus, etc.)
  // we fall back to writing the plaintext value into the INI — same behaviour as
  // a build without keychain support. The pre-wipe snapshot of old INI keys is
  // used to drop keychain entries for credentials the user removed via the UI.
#ifdef KARTEND_HAVE_QTKEYCHAIN
  QStringList preWipeKeys;
  {
    s.beginGroup(keys::kGroupScrapers);
    preWipeKeys = s.allKeys();
    s.endGroup();
  }
  QSet<QString> retainedKeys;
#endif
  s.remove(keys::kGroupScrapers);
  s.beginGroup(keys::kGroupScrapers);
  // Kartend-ztc64: recomputed on every save. Each save retries the keychain
  // write for every credential, so a recovered keychain re-promotes any
  // previously-demoted plaintext value and this stays empty — the demotion
  // self-heals without user action. First failure reason wins (one banner,
  // not one per field).
  QString newDemotionReason;
  for (auto pIt = m_generalSettings.scraper.credentials.constBegin();
       pIt != m_generalSettings.scraper.credentials.constEnd(); ++pIt) {
    const QString &providerId = pIt.key();
    if (providerId.trimmed().isEmpty()) continue;
    for (auto fIt = pIt.value().constBegin(); fIt != pIt.value().constEnd(); ++fIt) {
      const QString &field = fIt.key();
      if (field.trimmed().isEmpty()) continue;
      // Skip empty values so a fully-cleared field doesn't write an empty row
      // that survives a round-trip.
      if (fIt.value().isEmpty()) continue;
      const QString fullKey = providerId + QLatin1Char('/') + field;
#ifdef KARTEND_HAVE_QTKEYCHAIN
      QString writeError;
      if (syncWriteKeychain(fullKey, fIt.value(), &writeError)) {
        s.setValue(fullKey, QLatin1String(kKeychainSentinel));
        retainedKeys.insert(fullKey);
      } else {
        // No backend available — fall back to plaintext INI (matches a build
        // without keychain support; the security improvement is best-effort,
        // not load-bearing). Record the demotion so the settings dialog can
        // surface a non-modal banner instead of this log-only breadcrumb.
        qCWarning(lcSettingsManager) << "Keychain write failed for" << fullKey << "(" << writeError
                                     << "); falling back to plaintext INI";
        s.setValue(fullKey, fIt.value());
        if (newDemotionReason.isEmpty()) {
          newDemotionReason =
              writeError.isEmpty() ? QStringLiteral("keychain unavailable") : writeError;
        }
      }
#else
      // No keychain support compiled in: this is plaintext, unconditionally
      // and for every credential. Flag it the same way a runtime failure is
      // flagged — this arm used to set nothing, which meant the demotion
      // marker stayed empty and the banner took its hide() branch forever. The
      // configuration with the weakest storage (100% plaintext, 100% of the
      // time) was the ONLY one that never warned, while showing the same
      // reassuring masked password field as a keychain build (Kartend-4ahok).
      s.setValue(fullKey, fIt.value());
      newDemotionReason = QLatin1String(keys::kCredentialDemotionNoKeychainBuild);
#endif
    }
  }
  // Persist the demotion marker inside [Scrapers] (the group wipe above
  // removed any previous copy, so omitting the write doubles as the clear).
  // NOT under the ifdef: the non-keychain build needs its marker written too,
  // which is the whole point of the fix above.
  if (!newDemotionReason.isEmpty()) {
    s.setValue(keys::kCredentialDemotionReason, newDemotionReason);
  }
  m_credentialDemotionReason = newDemotionReason;
  s.endGroup();
#ifdef KARTEND_HAVE_QTKEYCHAIN
  // Sweep keychain entries the user removed in the UI. preWipeKeys is the union
  // of keys present before the wipe (legacy plaintext + existing @keychain
  // sentinels); anything not retained this round gets a delete. EntryNotFound
  // from the delete is fine — it means we already cleaned up on a prior save.
  // Meta keys (no provider/field slash, e.g. credentialDemotionReason) never
  // had a keychain entry, so skip them rather than issue a pointless RPC.
  for (const QString &k : preWipeKeys) {
    if (!k.contains(QLatin1Char('/'))) continue;
    if (!retainedKeys.contains(k)) {
      syncDeleteKeychain(k);
    }
  }
#endif

  // Scraper performance + behavior options. Wipe the group first so a "Reset to
  // defaults" round-trip doesn't leave stale custom keys.
  s.remove(keys::kGroupScraperOptions);
  s.beginGroup(keys::kGroupScraperOptions);
  ScraperSettingsPersistence::saveOptions(s, m_generalSettings.scraper.options);
  s.endGroup();
}
