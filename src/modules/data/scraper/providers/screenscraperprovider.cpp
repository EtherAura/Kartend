// ScreenScraper.fr provider. Reads dev+user creds from
// GeneralSettings::scraperCredentials at every API call; resolves
// the per-collection systemeid via the autodetect helper when no
// manual override is set. Stage 1: filename-based ROM identification
// only — hash- and archive-based ID are separate follow-up issues.
#include "screenscraperprovider.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>
#include <QUrl>
#include <QUrlQuery>

#include "bundledcredentials.h"
#include "collectionutils.h"
#include "datcache.h"
#include "datlookup.h"
#include "httpclient.h"
#include "romhasher.h"
#include "screenscraperparser.h"
#include "screenscrapersystems.h"

namespace {

// Logging category for provider-side issues that fall outside the HTTP /
// timings / persistence categories that already exist. Warnings here are
// always-on (default QtWarningMsg) so a silent ROM-hash failure can't slip
// past unnoticed again (Kartend-ou0a).
Q_LOGGING_CATEGORY(lcScreenScraperProvider, "kartend.scraper.screenscraper")

// API endpoints live under api.screenscraper.fr — the public site at
// www.screenscraper.fr is the human-facing browse UI and does not
// answer the api2/* paths we hit here.
constexpr const char *SS_HOST = "api.screenscraper.fr";

// Detect a No-Intro-style region tag from a ROM filename or basename
// (Kartend-ou0a). When hash-based ID can't run (e.g. archive extraction
// timed out on a multi-GB PS2 .zip), the SS filename-only match often
// lands on the canonical (US) jeu record even when the filename made
// the region explicit. Returning the detected SS-shortname here lets
// the parser put it ahead of SS's matched-ROM region in the preference
// chain. Returns empty when nothing matches — caller treats that as
// "no override, trust SS".
//
// Recognises (in this order of confidence): parenthesised full names
// like "(Japan)", "(USA)", "(Europe)"; single-letter shorthand like
// "(J)", "(U)", "(E)" — common in older releases; and multi-region
// tags like "(Japan, USA)" (first token wins). Case-insensitive.
QString detectRegionFromFilename(const QString &filenameOrBasename) {
  static const QList<QPair<QRegularExpression, QString>> kPatterns = {
      // Full names (No-Intro convention). Matched as whole-word inside
      // a parenthesised tag, possibly followed by a comma + more
      // regions.
      {QRegularExpression(QStringLiteral("\\(Japan(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("jp")},
      {QRegularExpression(QStringLiteral("\\bUSA(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("us")},
      {QRegularExpression(QStringLiteral("\\bEurope(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("eu")},
      {QRegularExpression(QStringLiteral("\\bWorld(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("wor")},
      {QRegularExpression(QStringLiteral("\\bKorea(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("kr")},
      {QRegularExpression(QStringLiteral("\\bFrance(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("fr")},
      {QRegularExpression(QStringLiteral("\\bGermany(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("de")},
      {QRegularExpression(QStringLiteral("\\bItaly(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("it")},
      {QRegularExpression(QStringLiteral("\\bSpain(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("sp")},
      {QRegularExpression(QStringLiteral("\\bBrazil(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("br")},
      // Single-letter shorthand. Anchored to '(L)' exactly to avoid
      // matching '(J-Pop)' or similar incidental text.
      {QRegularExpression(QStringLiteral("\\(J\\)")), QStringLiteral("jp")},
      {QRegularExpression(QStringLiteral("\\(U\\)")), QStringLiteral("us")},
      {QRegularExpression(QStringLiteral("\\(E\\)")), QStringLiteral("eu")},
      {QRegularExpression(QStringLiteral("\\(W\\)")), QStringLiteral("wor")},
      {QRegularExpression(QStringLiteral("\\(K\\)")), QStringLiteral("kr")},
      {QRegularExpression(QStringLiteral("\\(F\\)")), QStringLiteral("fr")},
      {QRegularExpression(QStringLiteral("\\(G\\)")), QStringLiteral("de")},
      {QRegularExpression(QStringLiteral("\\(I\\)")), QStringLiteral("it")},
      {QRegularExpression(QStringLiteral("\\(S\\)")), QStringLiteral("sp")},
      {QRegularExpression(QStringLiteral("\\(B\\)")), QStringLiteral("br")},
  };
  for (const auto &[re, tag] : kPatterns) {
    if (re.match(filenameOrBasename).hasMatch()) {
      return tag;
    }
  }
  return {};
}
// SS serves media files from a separate CDN host (`neoclone.screenscraper.fr`).
// Without an explicit policy this host defaults to maxConcurrent=1 in
// HttpClient, so every cover/screenshot/fanart download serialized
// behind the previous one — the symptom in /tmp/scrape.log was 27
// queued media requests all sitting at inflight=1 even after the
// dialog dispatched them in parallel.
constexpr const char *SS_MEDIA_HOST = "neoclone.screenscraper.fr";
constexpr const char *SS_JEUINFOS = "https://api.screenscraper.fr/api2/jeuInfos.php";
// API-host pacing for jeuInfos.php. Fixed (one call per scrape, not
// the bottleneck). Media-host pacing is *dynamic* and pulled from
// GeneralSettings.scraperOptions on every fetch so the user can dial
// it from the Scraper settings panel without restarting.
constexpr int SS_API_RATE_LIMIT_MS = 250;
constexpr int SS_API_MAX_CONCURRENT = 2;

constexpr const char *SS_PROVIDER_ID = "screenscraper";
constexpr const char *SS_FIELD_DEV_ID = "dev_id";
constexpr const char *SS_FIELD_DEV_PASSWORD = "dev_password";
constexpr const char *SS_FIELD_USER_ID = "user_id";
constexpr const char *SS_FIELD_USER_PASSWORD = "user_password";

QString userAgent() {
  return QStringLiteral("Kartend/%1 (https://github.com/EtherAura/Kartend)")
      .arg(QString::fromLatin1(APP_VERSION));
}

// Re-applied on every fetchMediaBytes so the user can change the
// concurrency/throttle settings live. API host stays at compile-time
// defaults; the media host honors `mediaConcurrency` + `mediaThrottleMs`
// from the settings struct, clamped to safe ranges.
void registerHostThrottles(const GeneralSettings *settings) {
  auto *client = Scraper::HttpClient::instance();
  client->setRateLimit(QString::fromLatin1(SS_HOST), SS_API_RATE_LIMIT_MS, SS_API_MAX_CONCURRENT);
  int mediaConc = 6;
  int mediaThrottle = 100;
  if (settings) {
    mediaConc = std::clamp(settings->scraperOptions.mediaConcurrency, 1, 16);
    mediaThrottle = std::clamp(settings->scraperOptions.mediaThrottleMs, 0, 5000);
  }
  client->setRateLimit(QString::fromLatin1(SS_MEDIA_HOST), mediaThrottle, mediaConc);
}

ErrorUtils::ErrorContext notConfiguredError() {
  return ErrorUtils::ErrorContext::error(
      ErrorUtils::ErrorCode::InvalidArgument,
      QStringLiteral("ScreenScraper developer credentials are not available. "
                     "This Kartend build was packaged without bundled SS dev "
                     "credentials. Apply for your own at the SS forum's "
                     "development section, then paste the issued dev_id and "
                     "dev_password under Settings → Scrapers → ScreenScraper."),
      "ScreenScraperProvider");
}

// SS API v2 returns specific HTTP status codes for distinct failure
// modes (see api.screenscraper.fr docs). Re-map the upstream's
// generic "HTTP request failed" + French response body into a single
// English sentence the dialog can show without dumping the raw blob
// at the user. Returns the original error untouched when the status
// code isn't one SS overloads (regular 5xx, network-level timeouts,
// etc.) so unexpected failures still surface their underlying detail.
ErrorUtils::ErrorContext mapScreenScraperHttpError(const ErrorUtils::ErrorContext &original) {
  if (original.httpStatus <= 0) return original;
  QString message;
  switch (original.httpStatus) {
  case 400:
    message = QStringLiteral("ScreenScraper rejected the request as malformed (HTTP 400). "
                             "Likely causes: an invalid system id, a missing required field, "
                             "or a path component the server refused.");
    break;
  case 401:
    message = QStringLiteral(
        "ScreenScraper closed its API to non-members because the server is "
        "overloaded (HTTP 401). Try again later, or sign in with member "
        "credentials under Settings → Scrapers → ScreenScraper for priority access.");
    break;
  case 403:
    message = QStringLiteral(
        "ScreenScraper rejected the developer credentials (HTTP 403). "
        "Verify the dev_id and dev_password under Settings → Scrapers → ScreenScraper.");
    break;
  case 404:
    message = QStringLiteral("ScreenScraper has no entry for this game (HTTP 404).");
    break;
  case 423:
    message = QStringLiteral("ScreenScraper infrastructure is currently down (HTTP 423). "
                             "Their service is unavailable — try again later.");
    break;
  case 426:
    message = QStringLiteral("This Kartend build was blocked by ScreenScraper (HTTP 426). "
                             "Update Kartend to the latest version — older releases are "
                             "occasionally blacklisted when their API requests fall behind a "
                             "breaking change.");
    break;
  case 429: {
    QString tail = QStringLiteral(" Reduce concurrent scrapes or wait before retrying.");
    if (original.retryAfterSeconds > 0) {
      tail = QStringLiteral(" Server asked us to wait %1 second(s) before retrying.")
                 .arg(original.retryAfterSeconds);
    }
    message = QStringLiteral("ScreenScraper rate-limited the request (HTTP 429).") + tail;
    break;
  }
  case 430:
    message = QStringLiteral("ScreenScraper's daily request quota for this account is "
                             "exhausted (HTTP 430). The quota resets at midnight UTC.");
    break;
  case 431:
    message = QStringLiteral("ScreenScraper hit the daily failed-lookup quota for this "
                             "account (HTTP 431). Too many ROMs in this collection don't "
                             "match anything in the SS database — fix the collection's "
                             "system id or the file naming, then try again tomorrow.");
    break;
  default:
    return original;
  }
  auto remapped = ErrorUtils::ErrorContext::error(original.code != ErrorUtils::ErrorCode::Success
                                                      ? original.code
                                                      : ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                                  message,
                                                  original.source.isEmpty()
                                                      ? QStringLiteral("ScreenScraperProvider")
                                                      : original.source)
                      .withHttpStatus(original.httpStatus);
  if (original.retryAfterSeconds > 0) {
    remapped.withRetryAfter(original.retryAfterSeconds);
  }
  if (!original.details.isEmpty()) {
    remapped.withDetails(original.details);
  }
  return remapped;
}

} // namespace

ScreenScraperProvider::ScreenScraperProvider(GeneralSettingsAccessor settingsAccessor,
                                             CollectionAccessor collectionAccessor)
    : m_settingsAccessor(std::move(settingsAccessor)),
      m_collectionAccessor(std::move(collectionAccessor)),
      m_catalog(
          Scraper::HttpClient::instance(), userAgent(), [this]() { return currentCredentials(); },
          &mapScreenScraperHttpError) {
  registerHostThrottles(m_settingsAccessor ? m_settingsAccessor() : nullptr);
}

ScreenScraperProvider::Credentials ScreenScraperProvider::currentCredentials() const {
  Credentials c;
  if (m_settingsAccessor) {
    if (const GeneralSettings *settings = m_settingsAccessor()) {
      const auto blob = settings->scraperCredentials.value(QString::fromLatin1(SS_PROVIDER_ID));
      c.devId = blob.value(QString::fromLatin1(SS_FIELD_DEV_ID));
      c.devPassword = blob.value(QString::fromLatin1(SS_FIELD_DEV_PASSWORD));
      c.userId = blob.value(QString::fromLatin1(SS_FIELD_USER_ID));
      c.userPassword = blob.value(QString::fromLatin1(SS_FIELD_USER_PASSWORD));
    }
  }
  // Fall back to bundled dev credentials when the user hasn't supplied
  // their own. Empty bundled values (the default until the SS forum
  // application is approved) leave the dev fields empty, which the
  // caller treats as "credentials not configured".
  if (c.devId.isEmpty() || c.devPassword.isEmpty()) {
    const auto bundled = BundledCredentials::screenscraper();
    if (c.devId.isEmpty()) c.devId = bundled.devId;
    if (c.devPassword.isEmpty()) c.devPassword = bundled.devPassword;
  }
  return c;
}

int ScreenScraperProvider::resolveSystemId(
    const QList<ScreenScraperSystems::System> &systems) const {
  if (!m_collectionAccessor) return 0;
  const CollectionConfig *cfg = m_collectionAccessor();
  if (!cfg) return 0;
  if (cfg->scraperOverrides.screenscraperSystemId >= 0) {
    return cfg->scraperOverrides.screenscraperSystemId;
  }
  // Autodetect runs over the runtime-fetched catalog. When the
  // catalog is empty (offline / fetch failed / no creds) autodetect
  // returns -1 and we fall through to the SS "any system" sentinel.
  const int autodetected =
      ScreenScraperSystems::autodetect(cfg->name, cfg->type, cfg->extensions, systems);
  if (autodetected > 0) {
    return autodetected;
  }
  return 0;
}

// ensureSystemsCatalog + ensureMediaTypeCatalog moved to
// ScreenScraperCatalogManager — see screenscrapercatalogmanager.{h,cpp}.
// The provider now forwards via m_catalog and reads back labels through
// m_catalog.mediaTypeLabels() in buildParseOptions.

QUrl ScreenScraperProvider::searchUrl(const QString &query) const {
  if (query.trimmed().isEmpty()) return {};
  const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(query.trimmed()));
  return QUrl(
      QStringLiteral("https://www.screenscraper.fr/gameinfos.php?nomrecherche=%1").arg(encoded));
}

void ScreenScraperProvider::lookup(const QString &query, LookupCallback callback) {
  // Filename-only path: no source file → no hashing.
  runLookup(query, /*filePath=*/QString(), std::move(callback));
}

void ScreenScraperProvider::lookup(const LookupContext &ctx, LookupCallback callback) {
  // Hash-aware path: filePath comes from the context-menu callsite
  // when the right-clicked item resolves to a real on-disk file.
  runLookup(ctx.query, ctx.filePath, std::move(callback));
}

void ScreenScraperProvider::runLookup(const QString &query, const QString &filePath,
                                      LookupCallback callback) {
  if (!callback) return;
  const QString trimmed = query.trimmed();
  if (trimmed.isEmpty()) {
    callback(QList<Scraper::ScrapeCandidate>{});
    return;
  }
  const Credentials creds = currentCredentials();
  // SS rejects every API request without dev_id + dev_password. A
  // regular SS.fr user account (ssid + sspassword) is optional and
  // only adds the user's per-account rate-limit tier on top of the
  // dev creds — it does not authenticate the request on its own.
  if (creds.devId.isEmpty() || creds.devPassword.isEmpty()) {
    callback(notConfiguredError());
    return;
  }

  // No file → no hashing, jump straight to the main-thread tail.
  if (filePath.isEmpty()) {
    runLookupAfterHash(trimmed, RomHasher::Result{}, std::move(callback));
    return;
  }

  // Honour the user's hash-mode policy (Kartend-ou0a). Never → skip the
  // worker entirely. SizeGated → measure the file (or, for archives, the
  // archive itself — SS hashes the inner ROM so the archive size is a
  // proxy) and skip when over the threshold. Always (default) → proceed.
  auto hashMode = GeneralSettings::ScraperOptions::ScraperHashMode::Always;
  int maxHashableSizeMB = 4096;
  if (m_settingsAccessor) {
    if (const GeneralSettings *settings = m_settingsAccessor()) {
      hashMode = settings->scraperOptions.hashMode;
      maxHashableSizeMB = settings->scraperOptions.maxHashableSizeMB;
    }
  }
  if (hashMode == GeneralSettings::ScraperOptions::ScraperHashMode::Never) {
    runLookupAfterHash(trimmed, RomHasher::Result{}, std::move(callback));
    return;
  }
  if (hashMode == GeneralSettings::ScraperOptions::ScraperHashMode::SizeGated) {
    const qint64 sizeBytes = QFileInfo(filePath).size();
    const qint64 limitBytes = static_cast<qint64>(maxHashableSizeMB) * 1024 * 1024;
    if (sizeBytes > limitBytes) {
      qCInfo(lcScreenScraperProvider).nospace()
          << "Skipping ROM hash for " << filePath << " (" << sizeBytes
          << " bytes) — over the user's maxHashableSizeMB=" << maxHashableSizeMB
          << " limit. Falling back to filename-based matching.";
      runLookupAfterHash(trimmed, RomHasher::Result{}, std::move(callback));
      return;
    }
  }

  // Inner-ROM hashing for archives. The collection's
  // screenscraperHashArchive toggle (default on) controls whether
  // a .zip / .7z / etc. is unpacked first — SS indexes the dump
  // file's bytes, not the archive's, so inner hashing is what
  // actually lands the hash-ID match.
  bool wantInnerHash = true;
  if (m_collectionAccessor) {
    if (const CollectionConfig *cfg = m_collectionAccessor()) {
      wantInnerHash = cfg->scraperOverrides.screenscraperHashArchive;
    }
  }

  // Off-thread the hash. `RomHasher::hashArchiveInnerRom` runs an
  // external extractor via QProcess::waitForFinished — synchronous
  // and many seconds long for multi-GB archives. Doing that on the
  // main thread froze the entire UI per-batch-item (the stack trace
  // that caught this had main blocked in ppoll inside QProcess wait
  // while a 50K-item batch was running). The continuation runs on
  // the main thread via QFutureWatcher → qApp connection.
  //
  // Kartend-ou0a: report the stage on the GUI thread so the batch
  // progress view (and any future interactive overlay) can show
  // "Hashing ROM…" / "Extracting archive…" instead of a frozen
  // spinner during the multi-minute extraction.
  if (m_stageReporter) {
    const bool isArchive = wantInnerHash && RomHasher::isArchivePath(filePath);
    m_stageReporter(isArchive ? QObject::tr("Extracting archive for hash ID…")
                              : QObject::tr("Hashing ROM…"));
  }
  auto *watcher = new QFutureWatcher<RomHasher::Result>(qApp);
  QObject::connect(watcher, &QFutureWatcher<RomHasher::Result>::finished, qApp,
                   [this, watcher, trimmed, callback = std::move(callback)]() mutable {
                     const RomHasher::Result hashes = watcher->result();
                     watcher->deleteLater();
                     if (m_stageReporter) {
                       m_stageReporter(QString());
                     }
                     runLookupAfterHash(trimmed, hashes, std::move(callback));
                   });
  watcher->setFuture(QtConcurrent::run([wantInnerHash, filePath]() -> RomHasher::Result {
    auto r = (wantInnerHash && RomHasher::isArchivePath(filePath))
                 ? RomHasher::hashArchiveInnerRom(filePath)
                 : RomHasher::hashFile(filePath);
    if (r.isOk()) {
      return r.value();
    }
    // Kartend-ou0a: surface the failure cause rather than silently
    // falling back to filename-only matching. Without this log the only
    // observable symptom was a missing md5/sha1 in the jeuInfos.php URL
    // — easy to miss, and the next layer's SS response looked superficially
    // normal (just matched the wrong region's ROM record).
    qCWarning(lcScreenScraperProvider).nospace()
        << "ROM hashing failed; jeuInfos.php will fall back to filename-only "
           "matching. This is what produces wrong-region matches when a "
           "symlinked ROM resolves cleanly but Qt's QFile open path didn't "
           "follow it. error='"
        << r.error().message << "' details='" << r.error().details << "' file='" << filePath << "'";
    return RomHasher::Result{};
  }));
}

void ScreenScraperProvider::runLookupAfterHash(const QString &query,
                                               const RomHasher::Result &hashes,
                                               LookupCallback callback) {
  if (!callback) return;
  const QString trimmed = query.trimmed();
  const Credentials creds = currentCredentials();
  // Credentials may have changed between dispatch and continuation;
  // re-check the same gate so a config wipe mid-batch surfaces the
  // same error the synchronous path used to.
  if (creds.devId.isEmpty() || creds.devPassword.isEmpty()) {
    callback(notConfiguredError());
    return;
  }
  const bool hasUser = !creds.userId.isEmpty() && !creds.userPassword.isEmpty();

  const QString datCanonicalName = findDatCanonicalName(hashes);

  // Kick the media-type catalog refresh ahead of the lookup so a
  // first-run scrape lands without raw `bezel-16-9` style labels in
  // the result dialog. Best-effort and async — never blocks the
  // jeuInfos.php fetch, so the worst case is the very first scrape
  // still shows raw tags and subsequent scrapes show the friendly
  // labels.
  m_catalog.ensureMediaTypeCatalog();

  // Two-step async chain: ensure the systems catalog is loaded
  // (cached or freshly fetched), then resolve systemeid from it
  // and fire the actual jeuInfos.php query. Cold-start cost: one
  // extra roundtrip the first time the user scrapes (and every
  // CACHE_TTL_DAYS afterward); subsequent scrapes hit the disk
  // cache and skip straight to the real query.
  m_catalog.ensureSystemsCatalog([this, trimmed, creds, hashes, datCanonicalName, hasUser,
                                  callback = std::move(callback)](
                                     QList<ScreenScraperSystems::System> systems) mutable {
    const QString romnom =
        !datCanonicalName.isEmpty() ? datCanonicalName : QFileInfo(trimmed).fileName();
    const int systemeid = resolveSystemId(systems);
    // SS API guard: jeuInfos.php rejects (HTTP 400, body "Champ
    // systemeid obligatoire si aucun CRC") when systemeid is 0 AND
    // no rom hash accompanies the request. Used to silently pass
    // systemeid=0 as an "any system" sentinel; modern SS no longer
    // accepts that path. Catch it here with a user-actionable error
    // rather than burning a quota-counting request on a guaranteed
    // rejection.
    const bool haveHash = !hashes.md5.isEmpty() || !hashes.sha1.isEmpty();
    if (systemeid == 0 && !haveHash) {
      callback(
          ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                          QStringLiteral("Cannot identify ScreenScraper system for "
                                                         "this collection"),
                                          "ScreenScraperProvider::runLookup")
              .withDetails(QStringLiteral("ScreenScraper rejects jeuInfos.php requests that "
                                          "have neither a systemeid nor a ROM hash. Autodetect "
                                          "failed for this collection's name/type/extensions, "
                                          "and the file could not be hashed (typically because "
                                          "it's a multi-GB disc image or it doesn't resolve to "
                                          "a local path). Fix one of: (a) set "
                                          "`screenscraperSystemId` explicitly on the "
                                          "collection (Settings → Collections → Configuration "
                                          "tab), or (b) provide a smaller / hashable rom path "
                                          "so SS can match by hash.")));
      return;
    }
    const QUrl url = buildJeuInfosUrl(creds, romnom, systemeid, hashes, hasUser);

    // Kartend-ou0a: filename region detection. The user picks the policy:
    //   TrustScraperFirst: use filename region ONLY when no hash narrowed
    //     the candidate. SS's hash-matched region wins when present.
    //   FilenameWhenAvailable: filename region always wins when detectable
    //     (over SS's matched-ROM region). Use when SS's per-file region
    //     tagging is unreliable.
    //   ScraperOnly: never look at the filename. Pre-fix behaviour.
    const bool haveAnyHash = !hashes.md5.isEmpty() || !hashes.sha1.isEmpty();
    auto regionSource = GeneralSettings::ScraperOptions::ScraperRegionSource::TrustScraperFirst;
    if (m_settingsAccessor) {
      if (const GeneralSettings *settings = m_settingsAccessor()) {
        regionSource = settings->scraperOptions.regionSource;
      }
    }
    QString filenameRegionOverride;
    if (regionSource ==
            GeneralSettings::ScraperOptions::ScraperRegionSource::FilenameWhenAvailable ||
        (regionSource == GeneralSettings::ScraperOptions::ScraperRegionSource::TrustScraperFirst &&
         !haveAnyHash)) {
      filenameRegionOverride = detectRegionFromFilename(trimmed);
    }

    Scraper::HttpClient::instance()->get(
        url, userAgent(),
        [this, callback = std::move(callback),
         filenameRegionOverride](ErrorUtils::Result<QByteArray> response) mutable {
          handleJeuInfosResponse(std::move(response), std::move(callback), filenameRegionOverride);
        });
  });
}

void ScreenScraperProvider::handleJeuInfosResponse(ErrorUtils::Result<QByteArray> response,
                                                   LookupCallback callback,
                                                   const QString &filenameRegionOverride) {
  if (response.isError()) {
    callback(mapScreenScraperHttpError(response.error()));
    return;
  }
  // SS returns HTTP 200 with a plain-text French error body
  // ("Erreur de login : Verifier vos identifiants developpeur !"
  //  / "Erreur API : Acces non autorise !" / etc.) when auth or
  // quota fails. The JSON parser would surface these as opaque
  // "invalid response" errors — surface the real SS message
  // instead so the user sees what actually went wrong.
  const QByteArray bytes = response.value();
  // Diagnostic: when KARTEND_SCRAPER_DUMP_JSON is set, write each raw
  // jeuInfos.php response to disk so the exact SS payload shape (region
  // keys, media tags, …) can be inspected. Off by default; the file
  // path is logged so it is easy to find.
  if (qEnvironmentVariableIsSet("KARTEND_SCRAPER_DUMP_JSON")) {
    const QString dumpDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                .filePath(QStringLiteral("scraper-dump"));
    if (QDir().mkpath(dumpDir)) {
      const QString dumpPath = QDir(dumpDir).filePath(
          QStringLiteral("jeuInfos-%1.json").arg(QDateTime::currentMSecsSinceEpoch()));
      QFile dumpFile(dumpPath);
      if (dumpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        dumpFile.write(bytes);
        dumpFile.close();
        qWarning("[scraper-dump] wrote raw jeuInfos response to %s", qUtf8Printable(dumpPath));
      }
    }
  }
  const QString trimmedHead = QString::fromUtf8(bytes.left(64)).trimmed();
  if (!trimmedHead.startsWith('{') && trimmedHead.startsWith(QLatin1String("Erreur"))) {
    callback(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             QStringLiteral("ScreenScraper rejected the request"),
                                             "ScreenScraperProvider")
                 .withDetails(QString::fromUtf8(bytes).trimmed()));
    return;
  }
  // Refresh the cached quota from this response's `ssuser` block before
  // parsing the game data. Runs on the "no match" path too (SS still
  // reports the counters there), so the dialog's live readout stays
  // current even for a collection full of unmatched items.
  m_quota.updateFromResponse(bytes);
  // Parse twice: once for the candidate list (what the dialog shows)
  // and once for the full ScrapedItem (cached so fetchDetail() can
  // return it without a second roundtrip).
  auto parseOpts = buildParseOptions();
  parseOpts.filenameRegionOverride = filenameRegionOverride;
  auto cands = ScreenScraperParser::parseSearchResponse(bytes);
  if (cands.isOk() && !cands.value().isEmpty()) {
    auto detail = ScreenScraperParser::parseDetailResponse(bytes, parseOpts);
    if (detail.isOk()) {
      m_lastDetailId = cands.value().first().providerSpecificId;
      m_lastDetail = detail.value();
    }
  }
  callback(cands);
}

QString ScreenScraperProvider::findDatCanonicalName(const RomHasher::Result &hashes) {
  // The collection's `datFilePaths` is a priority-ordered list — we walk
  // it top-to-bottom, take the first hash hit, and use that DAT's
  // canonical romName as the SS `romnom` search query. SS recognises
  // canonical names far more reliably than messy library names (region
  // tags out of order, language abbreviations, dump tool prefixes, etc.).
  // Backed by an on-disk sqlite cache (DatCache) so cold-start lookup
  // against a 100MB MAME XML doesn't re-parse the XML every run. Failures
  // degrade silently per-entry: a missing / malformed / no-hit DAT just
  // moves to the next path in the list.
  if (!m_collectionAccessor) return QString();
  const CollectionConfig *cfg = m_collectionAccessor();
  if (!cfg || cfg->scraperOverrides.datFilePaths.isEmpty()) return QString();
  if (hashes.md5.isEmpty() && hashes.sha1.isEmpty()) return QString();
  if (!m_datCache) {
    m_datCache.emplace(DatCache::defaultPath());
  }
  if (!m_datCache->isOpen()) return QString();
  for (const QString &datPath : cfg->scraperOverrides.datFilePaths) {
    if (datPath.isEmpty()) continue;
    auto source = m_datCache->openOrIngest(datPath);
    if (source.isError()) continue;
    if (auto rec = m_datCache->lookup(source.value(), hashes.md5, hashes.sha1, hashes.crc)) {
      return rec->romName;
    }
  }
  return QString();
}

QUrl ScreenScraperProvider::buildJeuInfosUrl(const Credentials &creds, const QString &romnom,
                                             int systemeid, const RomHasher::Result &hashes,
                                             bool hasUser) const {
  QUrl url(QString::fromLatin1(SS_JEUINFOS));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("devid"), creds.devId);
  q.addQueryItem(QStringLiteral("devpassword"), creds.devPassword);
  q.addQueryItem(QStringLiteral("softname"), QStringLiteral("kartend"));
  q.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
  q.addQueryItem(QStringLiteral("romnom"), romnom);
  q.addQueryItem(QStringLiteral("systemeid"), QString::number(systemeid));
  if (!hashes.md5.isEmpty()) {
    q.addQueryItem(QStringLiteral("md5"), hashes.md5);
  }
  if (!hashes.sha1.isEmpty()) {
    q.addQueryItem(QStringLiteral("sha1"), hashes.sha1);
  }
  if (!hashes.crc.isEmpty()) {
    q.addQueryItem(QStringLiteral("crc"), hashes.crc);
  }
  if (hashes.size > 0) {
    q.addQueryItem(QStringLiteral("romtaille"), QString::number(hashes.size));
  }
  if (hasUser) {
    q.addQueryItem(QStringLiteral("ssid"), creds.userId);
    q.addQueryItem(QStringLiteral("sspassword"), creds.userPassword);
  }
  url.setQuery(q);
  return url;
}

ScreenScraperParser::ParseOptions ScreenScraperProvider::buildParseOptions() const {
  ScreenScraperParser::ParseOptions parseOpts;
  if (m_settingsAccessor) {
    if (const GeneralSettings *settings = m_settingsAccessor()) {
      // mediaMaxDim drives server-side image downscaling on SS — the parser
      // appends it to media URLs when set.
      parseOpts.mediaMaxDim = settings->scraperOptions.mediaMaxDimension;
      // JPG output is gated on the user's preset preference — only the
      // Fastest preset honors it (where the bandwidth win outweighs the
      // quality loss). The setting's value is checked here instead of
      // just-the-preset so a Custom user can opt in deliberately.
      parseOpts.preferJpg = settings->scraperOptions.preferJpgOutput;
      // Fallback region for region-keyed fields when the matched item's
      // own region has no entry. Each item still honours its own region
      // first inside the parser.
      parseOpts.preferredRegion = settings->scraperOptions.preferredScraperRegion;
    }
  }
  // Free-text fields (description, genres, ...) follow the application UI
  // language so they read consistently with the rest of Kartend. The app
  // loads translations off the system locale (see main.cpp), so derive the
  // language tag the same way rather than from a separate setting.
  parseOpts.preferredLanguage = QLocale().name().section(QLatin1Char('_'), 0, 0);
  parseOpts.mediaTypeLabels = m_catalog.mediaTypeLabels();
  return parseOpts;
}

// updateQuotaFromResponse moved to ScreenScraperQuotaManager —
// see screenscraperquotamanager.{h,cpp}.

void ScreenScraperProvider::fetchDetail(const Scraper::ScrapeCandidate &candidate,
                                        DetailCallback callback) {
  if (!callback) return;
  // Detail came in the same response as the lookup — return the cached
  // ScrapedItem when the cache key matches. The dialog always calls
  // fetchDetail with a candidate from the most recent lookup, so the
  // single-entry cache is sufficient.
  if (!m_lastDetailId.isEmpty() && candidate.providerSpecificId == m_lastDetailId) {
    callback(m_lastDetail);
    return;
  }
  callback(ErrorUtils::ErrorContext::error(
      ErrorUtils::ErrorCode::FileNotFound,
      "ScreenScraper detail cache miss — re-run the scrape to refresh",
      "ScreenScraperProvider::fetchDetail"));
}

void ScreenScraperProvider::fetchHealthStatus(HealthCallback callback) {
  ScreenScraperProviderHelpers::fetchHealthStatus(
      m_settingsAccessor ? m_settingsAccessor() : nullptr, std::move(callback));
}

void ScreenScraperProvider::fetchMediaBytes(const QUrl &url, MediaCallback callback) {
  if (!callback) return;
  // Re-apply the per-host throttle on every media fetch so changes in
  // the Scraper settings panel take effect without restarting. Cheap:
  // HttpClient::setRateLimit just replaces the policy entry.
  registerHostThrottles(m_settingsAccessor ? m_settingsAccessor() : nullptr);
  Scraper::HttpClient::instance()->get(
      url, userAgent(),
      [callback = std::move(callback)](ErrorUtils::Result<QByteArray> response) {
        if (response.isError()) {
          callback(mapScreenScraperHttpError(response.error()));
          return;
        }
        callback(response);
      },
      Scraper::HttpClient::kDefaultMaxResponseBytes,
      // Kartend-9ryx: media fetches must come back as image/*.
      // ScreenScraper has been observed to serve 200-OK HTML
      // "Access denied" pages under expired media URLs; the prefix
      // check turns those into structured errors instead of decode
      // failures.
      QStringLiteral("image/"));
}

namespace ScreenScraperProviderHelpers {

void fetchUserInfo(const GeneralSettings *settings, UserInfoCallback callback) {
  if (!callback) return;
  // Build credentials from the same path as the main provider so the
  // detected user-info exactly matches what scrapes will actually
  // send. dev_id / dev_password fall back to the bundled `cedar` key
  // when the user hasn't set their own; user_id / user_password are
  // strictly opt-in (no fallback).
  QString devId, devPassword, userId, userPassword;
  if (settings) {
    const auto blob = settings->scraperCredentials.value(QStringLiteral("screenscraper"));
    devId = blob.value(QStringLiteral("dev_id"));
    devPassword = blob.value(QStringLiteral("dev_password"));
    userId = blob.value(QStringLiteral("user_id"));
    userPassword = blob.value(QStringLiteral("user_password"));
  }
  if (devId.isEmpty() || devPassword.isEmpty()) {
    const auto bundled = BundledCredentials::screenscraper();
    if (devId.isEmpty()) devId = bundled.devId;
    if (devPassword.isEmpty()) devPassword = bundled.devPassword;
  }
  if (devId.isEmpty() || devPassword.isEmpty()) {
    callback(
        ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                        QStringLiteral("Developer credentials are not available"),
                                        "ScreenScraperProviderHelpers::fetchUserInfo"));
    return;
  }
  if (userId.isEmpty() || userPassword.isEmpty()) {
    callback(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             QStringLiteral("Member credentials not set"),
                                             "ScreenScraperProviderHelpers::fetchUserInfo"));
    return;
  }
  QUrl url(QStringLiteral("https://api.screenscraper.fr/api2/ssuserInfos.php"));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("devid"), devId);
  q.addQueryItem(QStringLiteral("devpassword"), devPassword);
  q.addQueryItem(QStringLiteral("softname"), QStringLiteral("kartend"));
  q.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
  q.addQueryItem(QStringLiteral("ssid"), userId);
  q.addQueryItem(QStringLiteral("sspassword"), userPassword);
  url.setQuery(q);
  Scraper::HttpClient::instance()->get(
      url, userAgent(), [callback = std::move(callback)](ErrorUtils::Result<QByteArray> response) {
        if (response.isError()) {
          callback(mapScreenScraperHttpError(response.error()));
          return;
        }
        callback(ScreenScraperParser::parseUserInfoResponse(response.value()));
      });
}

void fetchInfraInfo(const GeneralSettings *settings, InfraInfoCallback callback) {
  if (!callback) return;
  // Mirror fetchUserInfo's credential resolution so the probe runs
  // with exactly what subsequent scrapes will use. Dev creds are
  // required (SS rejects unauthenticated infra polls); user creds
  // are optional and only add the tier boost.
  QString devId, devPassword, userId, userPassword;
  if (settings) {
    const auto blob = settings->scraperCredentials.value(QStringLiteral("screenscraper"));
    devId = blob.value(QStringLiteral("dev_id"));
    devPassword = blob.value(QStringLiteral("dev_password"));
    userId = blob.value(QStringLiteral("user_id"));
    userPassword = blob.value(QStringLiteral("user_password"));
  }
  if (devId.isEmpty() || devPassword.isEmpty()) {
    const auto bundled = BundledCredentials::screenscraper();
    if (devId.isEmpty()) devId = bundled.devId;
    if (devPassword.isEmpty()) devPassword = bundled.devPassword;
  }
  if (devId.isEmpty() || devPassword.isEmpty()) {
    callback(
        ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                        QStringLiteral("Developer credentials are not available"),
                                        "ScreenScraperProviderHelpers::fetchInfraInfo"));
    return;
  }
  QUrl url(QStringLiteral("https://api.screenscraper.fr/api2/ssinfraInfos.php"));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("devid"), devId);
  q.addQueryItem(QStringLiteral("devpassword"), devPassword);
  q.addQueryItem(QStringLiteral("softname"), QStringLiteral("kartend"));
  q.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
  if (!userId.isEmpty() && !userPassword.isEmpty()) {
    q.addQueryItem(QStringLiteral("ssid"), userId);
    q.addQueryItem(QStringLiteral("sspassword"), userPassword);
  }
  url.setQuery(q);
  Scraper::HttpClient::instance()->get(
      url, userAgent(), [callback = std::move(callback)](ErrorUtils::Result<QByteArray> response) {
        if (response.isError()) {
          callback(mapScreenScraperHttpError(response.error()));
          return;
        }
        callback(ScreenScraperParser::parseInfraInfoResponse(response.value()));
      });
}

void fetchHealthStatus(const GeneralSettings *settings,
                       MetadataLookupProvider::HealthCallback callback) {
  if (!callback) return;
  // Whether the caller has user creds wired up — drives whether the
  // `closeforleecher` flag should refuse the scrape (anonymous tier is
  // the leecher tier in SS parlance) vs just warn.
  bool hasUserCreds = false;
  if (settings) {
    const auto blob = settings->scraperCredentials.value(QStringLiteral("screenscraper"));
    hasUserCreds = !blob.value(QStringLiteral("user_id")).isEmpty() &&
                   !blob.value(QStringLiteral("user_password")).isEmpty();
  }
  fetchInfraInfo(settings, [callback = std::move(callback), hasUserCreds](
                               ErrorUtils::Result<ScreenScraperParser::ScreenScraperInfraInfo> r) {
    using HealthStatus = MetadataLookupProvider::HealthStatus;
    if (r.isError()) {
      // Probe failure is non-fatal — the actual scrape will hit the
      // same error path and report it via mapScreenScraperHttpError.
      // Stay silent in the dialog rather than fearmongering on a
      // transient blip.
      callback(HealthStatus{});
      return;
    }
    const auto &info = r.value();
    HealthStatus out;
    // Refuse anonymous scrapes when SS has shut its API to the
    // leecher tier. Member scrapes still go through (SS allows them
    // on a separate path).
    if (info.closedForLeechers && !hasUserCreds) {
      out.refuseScrape = true;
      out.humanStatus =
          QObject::tr("ScreenScraper has closed its API to anonymous traffic right now. "
                      "Sign in with member credentials under Settings → Scrapers → ScreenScraper, "
                      "or try again later.");
      callback(out);
      return;
    }
    if (info.closedForNonMembers && !hasUserCreds) {
      out.refuseScrape = true;
      out.humanStatus = QObject::tr("ScreenScraper has closed its API to non-members right now "
                                    "(server overloaded). Sign in with member credentials, or try "
                                    "again later.");
      callback(out);
      return;
    }
    // Surface load info when any of the CPU figures are alarming or
    // scraper count is high. Threshold is intentionally loose — we
    // want to nudge the user about slow scrapes, not pepper them
    // with infra trivia on a quiet day.
    const int peakCpu = std::max({info.cpu1Percent, info.cpu2Percent, info.cpu3Percent});
    if (peakCpu >= 70 || info.activeScrapers >= 200) {
      QStringList parts;
      if (peakCpu > 0) {
        parts << QObject::tr("CPU %1%").arg(peakCpu);
      }
      if (info.activeScrapers > 0) {
        parts << QObject::tr("%1 active scrapers").arg(info.activeScrapers);
      }
      out.humanStatus = QObject::tr("ScreenScraper is busy right now (%1) — "
                                    "expect slower downloads.")
                            .arg(parts.join(QStringLiteral(", ")));
    }
    callback(out);
  });
}

} // namespace ScreenScraperProviderHelpers
