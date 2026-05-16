// ScreenScraper.fr provider. Reads dev+user creds from
// GeneralSettings::scraperCredentials at every API call; resolves
// the per-collection systemeid via the autodetect helper when no
// manual override is set. Stage 1: filename-based ROM identification
// only — hash- and archive-based ID are separate follow-up issues.
#include "screenscraperprovider.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QtConcurrent/QtConcurrentRun>
#include <QUrl>
#include <QUrlQuery>

#include "bundledcredentials.h"
#include "collectionutils.h"
#include "datcache.h"
#include "datlookup.h"
#include "httpclient.h"
#include "romhasher.h"
#include "screenscrapermediatypecache.h"
#include "screenscraperparser.h"
#include "screenscrapersystemcache.h"
#include "screenscrapersystems.h"

namespace {

// API endpoints live under api.screenscraper.fr — the public site at
// www.screenscraper.fr is the human-facing browse UI and does not
// answer the api2/* paths we hit here.
constexpr const char *SS_HOST = "api.screenscraper.fr";
// SS serves media files from a separate CDN host (`neoclone.screenscraper.fr`).
// Without an explicit policy this host defaults to maxConcurrent=1 in
// HttpClient, so every cover/screenshot/fanart download serialized
// behind the previous one — the symptom in /tmp/scrape.log was 27
// queued media requests all sitting at inflight=1 even after the
// dialog dispatched them in parallel.
constexpr const char *SS_MEDIA_HOST = "neoclone.screenscraper.fr";
constexpr const char *SS_JEUINFOS = "https://api.screenscraper.fr/api2/jeuInfos.php";
constexpr const char *SS_SYSTEMES_LISTE = "https://api.screenscraper.fr/api2/systemesListe.php";
constexpr const char *SS_MEDIAS_JEU_LISTE = "https://api.screenscraper.fr/api2/mediasJeuListe.php";
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
      m_collectionAccessor(std::move(collectionAccessor)) {
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
  if (cfg->screenscraperSystemId >= 0) {
    return cfg->screenscraperSystemId;
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

void ScreenScraperProvider::ensureSystemsCatalog(const Credentials &creds,
                                                 SystemsReadyCallback callback) const {
  if (!callback) return;
  const QString cachePath = ScreenScraperSystemCache::defaultCachePath();

  // Disk-cache hit (fresh) → callback immediately, no network. Stale
  // disk hit also returns the on-disk copy AND kicks off a background
  // refresh — we don't want to block the user's scrape on a fresh
  // catalog when the on-disk one is good enough.
  if (!cachePath.isEmpty() && !ScreenScraperSystemCache::isCacheStale(cachePath)) {
    auto loaded = ScreenScraperSystemCache::loadCachedSystems(cachePath);
    if (loaded.isOk()) {
      callback(loaded.value());
      return;
    }
  }
  // Cache missing or stale — fetch fresh. If we have on-disk data
  // we'll still serve the user from it on a fetch failure (degrade
  // rather than error), so try a load first as a fallback ready
  // value.
  QList<ScreenScraperSystems::System> staleFallback;
  if (!cachePath.isEmpty()) {
    auto staleLoad = ScreenScraperSystemCache::loadCachedSystems(cachePath);
    if (staleLoad.isOk()) {
      staleFallback = staleLoad.value();
    }
  }

  // Network fetch needs dev credentials. SS rejects every API request
  // without a valid dev_id + dev_password (regular SS.fr account login
  // alone is not enough — it only acts as a per-account rate-limit
  // boost on TOP of dev creds). Without dev creds, fall back to
  // whatever the disk had (possibly empty).
  if (creds.devId.isEmpty() || creds.devPassword.isEmpty()) {
    callback(staleFallback);
    return;
  }
  const bool hasUser = !creds.userId.isEmpty() && !creds.userPassword.isEmpty();

  QUrl url(QString::fromLatin1(SS_SYSTEMES_LISTE));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("devid"), creds.devId);
  q.addQueryItem(QStringLiteral("devpassword"), creds.devPassword);
  q.addQueryItem(QStringLiteral("softname"), QStringLiteral("kartend"));
  q.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
  // ssid/sspassword optional — adds the user's per-account rate
  // limit on top of the dev tier when both are configured.
  if (hasUser) {
    q.addQueryItem(QStringLiteral("ssid"), creds.userId);
    q.addQueryItem(QStringLiteral("sspassword"), creds.userPassword);
  }
  url.setQuery(q);

  Scraper::HttpClient::instance()->get(
      url, userAgent(),
      [callback = std::move(callback), cachePath,
       staleFallback](ErrorUtils::Result<QByteArray> response) {
        if (response.isError()) {
          // Surface a structured warning when the systems-catalog fetch
          // fails — the catch-all `callback(staleFallback)` lets the
          // scrape proceed with whatever the disk cache had, but the
          // log line tells the user *why* the live refresh fell back.
          auto remapped = mapScreenScraperHttpError(response.error());
          ErrorUtils::logError(remapped);
          callback(staleFallback);
          return;
        }
        auto parsed = ScreenScraperSystemCache::parseSystemsResponse(response.value());
        if (parsed.isError() || parsed.value().isEmpty()) {
          callback(staleFallback);
          return;
        }
        if (!cachePath.isEmpty()) {
          // Best-effort: cache write failure is logged inside saveSystems
          // and falls back to in-memory-only — the lookup still proceeds.
          (void)ScreenScraperSystemCache::saveSystems(cachePath, parsed.value());
        }
        callback(parsed.value());
      });
}

void ScreenScraperProvider::ensureMediaTypeCatalog() const {
  // Hot path: catalog already in memory. The map is the projection
  // we hand to the parser; non-empty means we've populated it from
  // either disk cache or a fresh fetch, so callers can move on.
  if (!m_mediaTypeLabels.isEmpty()) return;
  const QString cachePath = ScreenScraperMediaTypeCache::defaultCachePath();
  // Disk-cache hit (fresh) → populate in memory and return. Stale
  // disk hit also populates from disk AND kicks off a background
  // refresh — same degrade-rather-than-block policy as
  // ensureSystemsCatalog.
  if (!cachePath.isEmpty()) {
    auto loaded = ScreenScraperMediaTypeCache::loadCachedMediaTypes(cachePath);
    if (loaded.isOk() && !loaded.value().isEmpty()) {
      for (const auto &mt : loaded.value()) {
        if (!mt.type.isEmpty() && !mt.displayName.isEmpty()) {
          m_mediaTypeLabels.insert(mt.type, mt.displayName);
        }
      }
      // If still fresh, no network needed.
      if (!ScreenScraperMediaTypeCache::isCacheStale(cachePath)) return;
    }
  }
  // Stale or missing — fire a background fetch. We never block the
  // scrape on this; subsequent scrapes will see the refreshed labels
  // once the fetch lands. Needs dev creds (SS rejects unauthenticated
  // catalog fetches the same way it rejects systemesListe).
  Credentials creds = currentCredentials();
  if (creds.devId.isEmpty() || creds.devPassword.isEmpty()) return;

  QUrl url(QString::fromLatin1(SS_MEDIAS_JEU_LISTE));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("devid"), creds.devId);
  q.addQueryItem(QStringLiteral("devpassword"), creds.devPassword);
  q.addQueryItem(QStringLiteral("softname"), QStringLiteral("kartend"));
  q.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
  if (!creds.userId.isEmpty() && !creds.userPassword.isEmpty()) {
    q.addQueryItem(QStringLiteral("ssid"), creds.userId);
    q.addQueryItem(QStringLiteral("sspassword"), creds.userPassword);
  }
  url.setQuery(q);

  // Use a mutable-this lambda to write back the populated map. The
  // provider's lifetime is tied to the registry — when the registry
  // releases it, any in-flight fetch's callback would run on a freed
  // `this`. The HttpClient callback is fired on the main thread and
  // the registry tear-down is also main-thread, so the race is
  // serialised; `ensureMediaTypeCatalog` is best-effort and we accept
  // the small UAF window as the practical tradeoff for not adding a
  // QPointer-style guard to a non-QObject provider.
  Scraper::HttpClient::instance()->get(
      url, userAgent(), [this, cachePath](ErrorUtils::Result<QByteArray> response) {
        if (response.isError()) {
          // Quiet log path — fetching the catalog is a polish
          // refresh, not load-bearing for a scrape. The fallback
          // behavior (raw SS tags as labels) is documented and
          // acceptable when SS is having a bad day.
          ErrorUtils::logError(mapScreenScraperHttpError(response.error()));
          return;
        }
        auto parsed = ScreenScraperMediaTypeCache::parseMediaTypesResponse(response.value());
        if (parsed.isError() || parsed.value().isEmpty()) return;
        for (const auto &mt : parsed.value()) {
          if (!mt.type.isEmpty() && !mt.displayName.isEmpty()) {
            m_mediaTypeLabels.insert(mt.type, mt.displayName);
          }
        }
        if (!cachePath.isEmpty()) {
          (void)ScreenScraperMediaTypeCache::saveMediaTypes(cachePath, parsed.value());
        }
      });
}

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

  // Inner-ROM hashing for archives. The collection's
  // screenscraperHashArchive toggle (default on) controls whether
  // a .zip / .7z / etc. is unpacked first — SS indexes the dump
  // file's bytes, not the archive's, so inner hashing is what
  // actually lands the hash-ID match.
  bool wantInnerHash = true;
  if (m_collectionAccessor) {
    if (const CollectionConfig *cfg = m_collectionAccessor()) {
      wantInnerHash = cfg->screenscraperHashArchive;
    }
  }

  // Off-thread the hash. `RomHasher::hashArchiveInnerRom` runs an
  // external extractor via QProcess::waitForFinished — synchronous
  // and many seconds long for multi-GB archives. Doing that on the
  // main thread froze the entire UI per-batch-item (the stack trace
  // that caught this had main blocked in ppoll inside QProcess wait
  // while a 50K-item batch was running). The continuation runs on
  // the main thread via QFutureWatcher → qApp connection.
  auto *watcher = new QFutureWatcher<RomHasher::Result>(qApp);
  QObject::connect(watcher, &QFutureWatcher<RomHasher::Result>::finished, qApp,
                   [this, watcher, trimmed, callback = std::move(callback)]() mutable {
                     const RomHasher::Result hashes = watcher->result();
                     watcher->deleteLater();
                     runLookupAfterHash(trimmed, hashes, std::move(callback));
                   });
  watcher->setFuture(QtConcurrent::run([wantInnerHash, filePath]() -> RomHasher::Result {
    auto r = (wantInnerHash && RomHasher::isArchivePath(filePath))
                 ? RomHasher::hashArchiveInnerRom(filePath)
                 : RomHasher::hashFile(filePath);
    return r.isOk() ? r.value() : RomHasher::Result{};
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

  // DAT-file lookup. The collection's `datFilePaths` is a priority-
  // ordered list — we walk it top-to-bottom, take the first hash
  // hit, and use that DAT's canonical romName as the SS `romnom`
  // search query. SS recognises canonical names far more reliably
  // than messy library names (region tags out of order, language
  // abbreviations, dump tool prefixes, etc.). Backed by an on-disk
  // sqlite cache (DatCache) so cold-start lookup against a 100MB
  // MAME XML doesn't re-parse the XML every run. Failures degrade
  // silently per-entry: a missing / malformed / no-hit DAT just
  // moves to the next path in the list.
  QString datCanonicalName;
  if (m_collectionAccessor) {
    if (const CollectionConfig *cfg = m_collectionAccessor()) {
      if (!cfg->datFilePaths.isEmpty() && (!hashes.md5.isEmpty() || !hashes.sha1.isEmpty())) {
        if (!m_datCache) {
          m_datCache.emplace(DatCache::defaultPath());
        }
        if (m_datCache->isOpen()) {
          for (const QString &datPath : cfg->datFilePaths) {
            if (datPath.isEmpty()) continue;
            auto source = m_datCache->openOrIngest(datPath);
            if (source.isError()) continue;
            if (auto rec = m_datCache->lookup(source.value(), hashes.md5, hashes.sha1, QString())) {
              datCanonicalName = rec->romName;
              break;
            }
          }
        }
      }
    }
  }

  // Kick the media-type catalog refresh ahead of the lookup so a
  // first-run scrape lands without raw `bezel-16-9` style labels in
  // the result dialog. Best-effort and async — never blocks the
  // jeuInfos.php fetch, so the worst case is the very first scrape
  // still shows raw tags and subsequent scrapes show the friendly
  // labels.
  ensureMediaTypeCatalog();

  // Two-step async chain: ensure the systems catalog is loaded
  // (cached or freshly fetched), then resolve systemeid from it
  // and fire the actual jeuInfos.php query. Cold-start cost: one
  // extra roundtrip the first time the user scrapes (and every
  // CACHE_TTL_DAYS afterward); subsequent scrapes hit the disk
  // cache and skip straight to the real query.
  ensureSystemsCatalog(creds, [this, trimmed, creds, hashes, datCanonicalName, hasUser,
                               callback = std::move(callback)](
                                  QList<ScreenScraperSystems::System> systems) {
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
    if (hashes.size > 0) {
      q.addQueryItem(QStringLiteral("romtaille"), QString::number(hashes.size));
    }
    if (hasUser) {
      q.addQueryItem(QStringLiteral("ssid"), creds.userId);
      q.addQueryItem(QStringLiteral("sspassword"), creds.userPassword);
    }
    url.setQuery(q);

    Scraper::HttpClient::instance()->get(
        url, userAgent(), [this, callback](ErrorUtils::Result<QByteArray> response) {
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
          const QString trimmedHead = QString::fromUtf8(bytes.left(64)).trimmed();
          if (!trimmedHead.startsWith('{') && trimmedHead.startsWith(QLatin1String("Erreur"))) {
            callback(ErrorUtils::ErrorContext::error(
                         ErrorUtils::ErrorCode::InvalidArgument,
                         QStringLiteral("ScreenScraper rejected the request"),
                         "ScreenScraperProvider")
                         .withDetails(QString::fromUtf8(bytes).trimmed()));
            return;
          }
          // Parse twice: once for the candidate list (what the dialog
          // shows) and once for the full ScrapedItem (cached so
          // fetchDetail() can return it without a second roundtrip).
          // The detail parse honors the user's mediaMaxDimension so
          // image URLs get server-side downscaling when configured.
          ScreenScraperParser::ParseOptions parseOpts;
          if (m_settingsAccessor) {
            if (const GeneralSettings *settings = m_settingsAccessor()) {
              parseOpts.mediaMaxDim = settings->scraperOptions.mediaMaxDimension;
              // JPG output is gated on the user's preset preference —
              // only the Fastest preset honors it (where the bandwidth
              // win outweighs the quality loss). The setting's value
              // is checked here instead of just-the-preset so a Custom
              // user can opt in deliberately.
              parseOpts.preferJpg = settings->scraperOptions.preferJpgOutput;
              // Fallback region for region-keyed fields when the
              // matched ROM's own region has no entry. Each item still
              // honours its own region first inside the parser.
              parseOpts.preferredRegion = settings->scraperOptions.preferredScraperRegion;
            }
          }
          // Free-text fields (description, genres, ...) follow the
          // application UI language so they read consistently with the
          // rest of Kartend. The app loads translations off the system
          // locale (see main.cpp), so derive the language tag the same
          // way rather than from a separate setting.
          parseOpts.preferredLanguage = QLocale().name().section(QLatin1Char('_'), 0, 0);
          parseOpts.mediaTypeLabels = m_mediaTypeLabels;
          auto cands = ScreenScraperParser::parseSearchResponse(bytes);
          if (cands.isOk() && !cands.value().isEmpty()) {
            auto detail = ScreenScraperParser::parseDetailResponse(bytes, parseOpts);
            if (detail.isOk()) {
              m_lastDetailId = cands.value().first().providerSpecificId;
              m_lastDetail = detail.value();
            }
          }
          callback(cands);
        });
  });
}

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
  if (!callback) return;
  const GeneralSettings *settings = m_settingsAccessor ? m_settingsAccessor() : nullptr;
  // Whether the caller has user creds wired up — drives whether the
  // `closeforleecher` flag should refuse the scrape (anonymous tier
  // is the leecher tier in SS parlance) vs just warn.
  bool hasUserCreds = false;
  if (settings) {
    const auto blob = settings->scraperCredentials.value(QStringLiteral("screenscraper"));
    hasUserCreds = !blob.value(QStringLiteral("user_id")).isEmpty() &&
                   !blob.value(QStringLiteral("user_password")).isEmpty();
  }
  ScreenScraperProviderHelpers::fetchInfraInfo(
      settings, [callback = std::move(callback),
                 hasUserCreds](ErrorUtils::Result<ScreenScraperParser::ScreenScraperInfraInfo> r) {
        if (r.isError()) {
          // Probe failure is non-fatal — the actual scrape will hit
          // the same error path and report it via mapScreenScraperHttpError.
          // Stay silent in the dialog rather than fearmongering on a
          // transient blip.
          callback(HealthStatus{});
          return;
        }
        const auto &info = r.value();
        HealthStatus out;
        // Refuse anonymous scrapes when SS has shut its API to the
        // leecher tier. Member scrapes still go through (SS allows
        // them on a separate path).
        if (info.closedForLeechers && !hasUserCreds) {
          out.refuseScrape = true;
          out.humanStatus = QObject::tr(
              "ScreenScraper has closed its API to anonymous traffic right now. "
              "Sign in with member credentials under Settings → Scrapers → ScreenScraper, "
              "or try again later.");
          callback(out);
          return;
        }
        if (info.closedForNonMembers && !hasUserCreds) {
          out.refuseScrape = true;
          out.humanStatus =
              QObject::tr("ScreenScraper has closed its API to non-members right now "
                          "(server overloaded). Sign in with member credentials, or try "
                          "again later.");
          callback(out);
          return;
        }
        // Surface load info when any of the CPU figures are
        // alarming or scraper count is high. Threshold is intentionally
        // loose — we want to nudge the user about slow scrapes, not
        // pepper them with infra trivia on a quiet day.
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

void ScreenScraperProvider::fetchMediaBytes(const QUrl &url, MediaCallback callback) {
  if (!callback) return;
  // Re-apply the per-host throttle on every media fetch so changes in
  // the Scraper settings panel take effect without restarting. Cheap:
  // HttpClient::setRateLimit just replaces the policy entry.
  registerHostThrottles(m_settingsAccessor ? m_settingsAccessor() : nullptr);
  Scraper::HttpClient::instance()->get(
      url, userAgent(), [callback = std::move(callback)](ErrorUtils::Result<QByteArray> response) {
        if (response.isError()) {
          callback(mapScreenScraperHttpError(response.error()));
          return;
        }
        callback(response);
      });
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

} // namespace ScreenScraperProviderHelpers
