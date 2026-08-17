// ScreenScraper.fr provider. Reads dev+user creds from
// GeneralSettings::scraper.credentials at every API call; resolves
// the per-collection systemeid via the autodetect helper when no
// manual override is set. Stage 1: filename-based ROM identification
// only — hash- and archive-based ID are separate follow-up issues.
#include "screenscraperprovider.h"

#include <algorithm>
#include <limits>
#include <atomic>
#include <utility>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QLoggingCategory>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>
#include <QUrl>

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "databaseschema.h"
#include "datcache.h"
#include "datlookup.h"
#include "filehashcache.h"
#include "httpclient.h"
#include "romhasher.h"
#include "screenscraperaccount.h"
#include "screenscrapercompanyregistry.h"
#include "screenscraperparser.h"
#include "screenscraperregion.h"
#include "screenscrapersystems.h"
#include "screenscraperurls.h"

namespace {

// Logging category for provider-side issues that fall outside the HTTP /
// timings / persistence categories that already exist. Warnings here are
// always-on (default QtWarningMsg) so a silent ROM-hash failure can't slip
// past unnoticed again (Kartend-ou0a).
Q_LOGGING_CATEGORY(lcScreenScraperProvider, "kartend.scraper.screenscraper")

// API-host pacing for jeuInfos.php. Fixed (one call per scrape, not
// the bottleneck). Media-host pacing is *dynamic* and pulled from
// GeneralSettings.scraper.options on every fetch so the user can dial
// it from the Scraper settings panel without restarting.
constexpr int SS_API_RATE_LIMIT_MS = 250;
constexpr int SS_API_MAX_CONCURRENT = 2;

// Common platform-media types served by mediaSysteme.php. Every token below
// is an exact nomcourt in the live mediasSystemeListe.php catalog (verified);
// buildSystemeMediaUrl appends the "(wor)" region qualifier. A type a system
// has no entry for answers 200 "NOMEDIA" (text/html), which the image/
// content-type gate turns into a non-fatal skip, so an over-broad list is
// safe. {api token, canonical MediaAsset type, user-visible label}.
struct PlatformMediaType {
  const char *apiToken;
  const char *canonicalType;
  const char *label;
  /// Config slot + in-role preference the coordinator wires from — declared
  /// HERE (the provider owns its type vocabulary) so the generic entity
  /// coordinator never has to know these strings (EntityArtRole contract).
  Scraper::EntityArtRole role;
  int rolePriority;
};
constexpr PlatformMediaType kPlatformMediaTypes[] = {
    // Only types that map to a CollectionConfig art field are requested — no
    // point spending a media-host request + the user's SS quota on art with no
    // home (e.g. controller art has no config slot) (Kartend-ckepd.3 review).
    // The catalog exposes 32 system media types; that is a reason to stop
    // requesting the ones a system does NOT have, not a reason to download all
    // 32 (Kartend-qzk1s).
    //
    // Logo: prefer the wheel, fall back to the monochrome logo. Background:
    // illustration, then the console photo, then `background`.
    {"wheel", "wheel", "Logo (wheel)", Scraper::EntityArtRole::Logo, 0},
    {"logo-monochrome", "logo", "Logo (monochrome)", Scraper::EntityArtRole::Logo, 1},
    // SVG logo variants (user request 2026-08-17): downloaded as STYLE
    // SOURCES for the navigation sidebar's monochrome/tinted icon modes —
    // real vector silhouettes instead of recoloured raster wheels — so their
    // role is None: written to _shared/<type>/, never wired into the config
    // slots (the raster wheel stays the collectionIcon; the tree probes these
    // sibling dirs at render time). Catalog-driven selection skips them on
    // systems that lack them (307 / 264 of 250 entries carry them).
    {"logo-svg", "logo-svg", "Logo (SVG)", Scraper::EntityArtRole::None, 0},
    {"logo-monochrome-svg", "logo-monochrome-svg", "Logo (monochrome SVG)",
     Scraper::EntityArtRole::None, 0},
    {"illustration", "illustration", "Console illustration", Scraper::EntityArtRole::Background, 0},
    {"photo", "photo", "Console photo", Scraper::EntityArtRole::Background, 1},
    // Added last deliberately (Kartend-qzk1s). Measured over the live catalog:
    // illustration-or-photo covers 211/250 systems and `background` covers
    // 212/250, but they are not the same 212 — 32 systems have `background`
    // and NEITHER of the other two. Appending it at the lowest priority is
    // therefore purely additive: it rescues those 32 (background coverage
    // 211 -> 243/250) and cannot change what any system already resolved to,
    // because a higher-priority candidate always wins when present. Whether
    // `background` deserves to be PROMOTED over illustration is a look-and-feel
    // call that needs eyes on real art, not a coverage argument — left alone.
    {"background", "background", "Console background", Scraper::EntityArtRole::Background, 2},
};

/// Region ranking for PLATFORM art. Deliberately much simpler than the
/// parser's per-ROM chain (buildRegionPreferences): a platform is not a ROM, so
/// there is no item region or filename marker to honour — it collapses to "the
/// user's configured region, then SS's world tag, then whatever exists". Lower
/// is better; kNoRegionMatch keeps unmatched variants selectable but last.
constexpr int kNoRegionMatch = 100;
int platformRegionRank(const QString &region, const QString &preferredRegion) {
  const QString r = region.trimmed().toLower();
  if (!preferredRegion.isEmpty() && r == preferredRegion.trimmed().toLower()) return 0;
  if (r == QLatin1String("wor")) return 1;
  return kNoRegionMatch;
}

/// Best catalog entry for `type` on this system, or nullptr when the system has
/// no such art. Returning nullptr is the whole point of Kartend-qzk1s: it means
/// the type is skipped instead of costing a media-host request that answers
/// NOMEDIA. Measured over the live catalog, that was 173 of every 1,000
/// requests a full-library platform scrape used to fire.
const ScreenScraperSystems::Media *bestCatalogMedia(const ScreenScraperSystems::System &sys,
                                                    const QString &type,
                                                    const QString &preferredRegion) {
  const ScreenScraperSystems::Media *best = nullptr;
  int bestRank = std::numeric_limits<int>::max();
  for (const auto &m : sys.media) {
    if (m.type != type) continue;
    // Every type in kPlatformMediaTypes is a still image; a video-endpoint row
    // would need mediaVideoSysteme.php, which this URL builder does not speak.
    if (m.video || m.token.isEmpty()) continue;
    const int rank = platformRegionRank(m.region, preferredRegion);
    if (rank < bestRank) {
      best = &m;
      bestRank = rank;
    }
  }
  return best;
}

// Re-applied on every fetchMediaBytes so the user can change the
// concurrency/throttle settings live. API host stays at compile-time
// defaults; the media host honors `mediaConcurrency` + `mediaThrottleMs`
// from the settings struct, clamped to safe ranges.
void registerHostThrottles(const GeneralSettings *settings) {
  auto *client = Scraper::HttpClient::instance();
  client->setRateLimit(QString::fromLatin1(ScreenScraperUrls::SS_HOST), SS_API_RATE_LIMIT_MS,
                       SS_API_MAX_CONCURRENT);
  int mediaConc = 6;
  int mediaThrottle = 100;
  if (settings) {
    mediaConc = std::clamp(settings->scraper.options.mediaConcurrency, 1, 16);
    mediaThrottle = std::clamp(settings->scraper.options.mediaThrottleMs, 0, 5000);
  }
  client->setRateLimit(QString::fromLatin1(ScreenScraperUrls::SS_MEDIA_HOST), mediaThrottle,
                       mediaConc);
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

} // namespace

ScreenScraperProvider::ScreenScraperProvider(GeneralSettingsAccessor settingsAccessor,
                                             CollectionAccessor collectionAccessor)
    : m_settingsAccessor(std::move(settingsAccessor)),
      m_collectionAccessor(std::move(collectionAccessor)),
      m_catalog(Scraper::HttpClient::instance(), userAgent(),
                [this]() { return currentCredentials(); },
                &ScreenScraperUrls::mapScreenScraperHttpError,
                {QString::fromLatin1(ScreenScraperUrls::SS_MEDIA_HOST_SUFFIX)}) {
  registerHostThrottles(m_settingsAccessor ? m_settingsAccessor() : nullptr);
}

ScreenScraperProvider::~ScreenScraperProvider() {
  // Kartend-s1s98: a ROM-hash task may still be running off-thread when our
  // owner (BatchScrapeRunner / ScraperService) drops the last shared_ptr.
  // Wait for it before our members tear down so the worker — and the
  // main-thread continuation it would trigger — can't touch freed state.
  // No-op when no hash is in flight / already finished.
  //
  // Kartend-37ei3: flip the in-flight task's cooperative cancel token first.
  // RomHasher / the archive extractor poll it, so this turns a close-mid-hash
  // from a multi-second (multi-GB ROM) / multi-minute (archive extract) main-
  // thread block into a prompt teardown. No-op when no hash is in flight.
  // Cancel + drain + delete EVERY in-flight hash (concurrent lookups can have
  // several; Kartend audit vrqzk — this was a single watcher). Flip all tokens
  // first so the off-thread hashes exit promptly, then wait on each future so
  // none outlives us touching shared state, then delete each watcher to sever
  // its continuation before our members tear down (the provider is not a
  // QObject, so nothing auto-deletes them). A watcher whose hash already
  // finished removed itself in the continuation, so it isn't here.
  const QList<QFutureWatcher<RomHasher::Result> *> watchers = m_inFlightHashes.keys();
  for (auto *w : watchers) {
    if (const std::shared_ptr<std::atomic<bool>> &tok = m_inFlightHashes.value(w)) {
      tok->store(true, std::memory_order_release);
    }
  }
  for (auto *w : watchers) {
    w->waitForFinished();
    delete w;
  }
  m_inFlightHashes.clear();
  // Pending transient-retry timers are torn down by ~ProviderBase, which now
  // owns the retry loop (Kartend-jjyst.10).
}

ScreenScraperProvider::Credentials ScreenScraperProvider::currentCredentials() const {
  const ScreenScraperProviderHelpers::SsCredentials r =
      ScreenScraperProviderHelpers::resolveSsCredentials(m_settingsAccessor ? m_settingsAccessor()
                                                                            : nullptr);
  Credentials c;
  c.devId = r.devId;
  c.devPassword = r.devPassword;
  c.userId = r.userId;
  c.userPassword = r.userPassword;
  return c;
}

void ScreenScraperProvider::fetchEntity(const Scraper::EntityScrapeTarget &target,
                                        DetailCallback callback) {
  if (target.type != Scraper::ScrapeEntityType::Platform) {
    if (callback) {
      callback(ErrorUtils::ErrorContext::error(
          ErrorUtils::ErrorCode::InvalidArgument,
          QStringLiteral("ScreenScraper only scrapes Platform entities (got entity type %1)")
              .arg(static_cast<int>(target.type)),
          QStringLiteral("ScreenScraperProvider::fetchEntity")));
    }
    return;
  }
  // The systemeid is either supplied by the caller (a re-queued failed entity
  // carries the id it already resolved) or left EMPTY to mean "resolve for this
  // collection" — the fresh UI launch (Kartend-ckepd.6). In the empty case it is
  // autodetected via resolveSystemId() inside the callback below: the same
  // override-then-heuristic path the per-game scrape uses, which needs the
  // runtime catalog.
  bool explicitOk = false;
  const int explicitId = target.identity.toInt(&explicitOk);
  const bool hasExplicitId = explicitOk && explicitId > 0;
  const Credentials creds = currentCredentials();
  // Resolve the system in the catalog (cold-start fetch handled by the catalog
  // manager) for its display name, then emit one media URL per known platform
  // media type. The liveness token guards against the provider being destroyed
  // mid-fetch — same pattern as runLookup (Kartend audit cr950).
  m_catalog.ensureSystemsCatalog(
      [this, hasExplicitId, explicitId, creds, alive = std::weak_ptr<int>(m_lifetimeToken),
       callback = std::move(callback)](const QList<ScreenScraperSystems::System> &systems) mutable {
        if (alive.expired()) {
          if (callback) {
            callback(ErrorUtils::ErrorContext::error(
                ErrorUtils::ErrorCode::OperationCancelled,
                QStringLiteral("ScreenScraper provider destroyed during platform catalog lookup"),
                QStringLiteral("ScreenScraperProvider::fetchEntity")));
          }
          return;
        }
        // ensureSystemsCatalog ALWAYS calls back with a list — empty on ANY
        // failure (no creds, null HTTP client, network error, parse/empty). An
        // empty list therefore means "catalog unavailable", NOT "this id is
        // absent". Mapping the former to RemoteResourceNotFound would let the
        // coordinator's notFound bucket silently consume every entity job when
        // offline / no-creds / cold-cache (cursor advances, nothing recorded,
        // not re-queueable). Return a transient, non-notFound error so it lands
        // in errors + failedItems and (with the entity discriminator) stays
        // re-queueable. Only a NON-empty catalog that genuinely lacks the id is
        // a real RemoteResourceNotFound.
        if (systems.isEmpty()) {
          if (callback) {
            callback(ErrorUtils::ErrorContext::error(
                         ErrorUtils::ErrorCode::UnknownError,
                         QStringLiteral("ScreenScraper systems catalog unavailable; cannot resolve "
                                        "the platform system"),
                         QStringLiteral("ScreenScraperProvider::fetchEntity"))
                         .withHttpStatus(503));
          }
          return;
        }
        // systemeid: caller-supplied (a re-queued entity carries its resolved
        // id) or resolved for this collection now the catalog is loaded — the
        // same override → autodetect path as the game scrape. A non-empty
        // catalog that yields nothing means the collection's platform is
        // undeterminable: a routine not-found (re-queueable), not an error.
        const int systemeid = hasExplicitId ? explicitId : resolveSystemId(systems);
        if (systemeid <= 0) {
          if (callback) {
            callback(ErrorUtils::ErrorContext::error(
                ErrorUtils::ErrorCode::RemoteResourceNotFound,
                QStringLiteral("Could not determine the ScreenScraper system for this collection; "
                               "set its System ID in the collection's scraper overrides"),
                QStringLiteral("ScreenScraperProvider::fetchEntity")));
          }
          return;
        }
        const ScreenScraperSystems::System *sys = ScreenScraperSystems::find(systems, systemeid);
        if (!sys) {
          // Catalog loaded but has no entry for this systemeid — a genuine,
          // routine "not found", not an error (Kartend-e8aag bucketing applies
          // to entity scrapes too).
          if (callback) {
            callback(ErrorUtils::ErrorContext::error(
                ErrorUtils::ErrorCode::RemoteResourceNotFound,
                QStringLiteral("ScreenScraper has no system with id %1").arg(systemeid),
                QStringLiteral("ScreenScraperProvider::fetchEntity")));
          }
          return;
        }
        Scraper::ScrapedItem item;
        item.sourceProviderId = id();
        item.title = sys->displayName;
        // User creds count only when BOTH ssid and sspassword are set —
        // matching runLookupAfterHash and the account probes. Gating on the
        // id alone would append `ssid=<id>&sspassword=` for a user with a
        // cleared password and fail SS login on every media URL.
        const bool hasUser = !creds.userId.isEmpty() && !creds.userPassword.isEmpty();
        // Kartend-qzk1s: when the system carries a media catalog, it tells us
        // exactly which art exists and under which region token, so we request
        // only what is there. An EMPTY catalog is not "this system has no art"
        // — it is "we don't know": a cache file written before Kartend-xny9o
        // kept medias[] stays fresh for the full 30-day TTL, so those users
        // must keep the pre-catalog behaviour (speculate every type, let the
        // NOMEDIA content-type gate discard the misses) until their cache
        // rolls over.
        const bool haveCatalog = !sys->media.isEmpty();
        const QString preferredRegion =
            m_settingsAccessor && m_settingsAccessor()
                ? m_settingsAccessor()->scraper.options.preferredScraperRegion
                : QString();
        for (const auto &mt : kPlatformMediaTypes) {
          const QString apiToken = QString::fromLatin1(mt.apiToken);
          QString mediaToken;
          if (haveCatalog) {
            const auto *entry = bestCatalogMedia(*sys, apiToken, preferredRegion);
            if (!entry) continue; // this system has no such art — don't spend a request
            mediaToken = entry->token;
          } else {
            // Pre-catalog form: the world tag the builder used to append itself.
            mediaToken = apiToken + QStringLiteral("(wor)");
          }
          Scraper::MediaAsset asset;
          asset.type = QString::fromLatin1(mt.canonicalType);
          asset.label = QString::fromLatin1(mt.label);
          asset.entityRole = mt.role;
          asset.entityRolePriority = mt.rolePriority;
          asset.url =
              ScreenScraperUrls::buildSystemeMediaUrl(creds, systemeid, mediaToken, hasUser);
          // Platform-scoped → persisted to the collection's _shared art dir as
          // `_shared/<type>/platform_<systemeid>.<ext>` (Kartend-ckepd.3). The
          // systemeid is numeric, so it is a safe path component.
          asset.scope = Scraper::MediaScope::Platform;
          asset.scopeKey = QString::number(systemeid);
          item.media.append(asset);
        }
        if (callback) callback(item);
      });
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
  // Filename-only path: no source file → no hashing, no cancel token needed.
  runLookup(query, /*filePath=*/QString(), /*cancelToken=*/{}, std::move(callback));
}

void ScreenScraperProvider::lookup(const LookupContext &ctx, LookupCallback callback) {
  // Hash-aware path: filePath comes from the context-menu callsite
  // when the right-clicked item resolves to a real on-disk file.
  runLookup(ctx.query, ctx.filePath, ctx.cancelToken, std::move(callback));
}

void ScreenScraperProvider::runLookup(const QString &query, const QString &filePath,
                                      const std::shared_ptr<std::atomic<bool>> &cancelToken,
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
  auto hashMode = ScraperOptions::ScraperHashMode::Always;
  int maxHashableSizeMB = 4096;
  if (m_settingsAccessor) {
    if (const GeneralSettings *settings = m_settingsAccessor()) {
      hashMode = settings->scraper.options.hashMode;
      maxHashableSizeMB = settings->scraper.options.maxHashableSizeMB;
    }
  }
  if (hashMode == ScraperOptions::ScraperHashMode::Never) {
    runLookupAfterHash(trimmed, RomHasher::Result{}, std::move(callback));
    return;
  }
  if (hashMode == ScraperOptions::ScraperHashMode::SizeGated) {
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
  // Per-lookup watcher (Kartend audit vrqzk). A SINGLE shared m_hashWatcher
  // made each concurrent lookup's disconnect()+setFuture() clobber the prior
  // in-flight lookup's continuation, so all but the last concurrent lookup's
  // callback was lost and its batch item leaked forever (scrape hung at <100%,
  // never emitting finished). Each hash now owns its watcher. The provider is
  // NOT a QObject, so the watcher gets no parent and uses itself as the
  // connection context (as the old member watcher did with &m_hashWatcher); it
  // is tracked in m_inFlightHashes so the destructor can cancel + drain + delete
  // every in-flight hash before our members tear down — severing the
  // continuation so it can't deref a freed `this` (Kartend-s1s98). The cancel
  // token is the map value so the destructor can flip it for a prompt
  // close-mid-hash teardown instead of a multi-minute block (Kartend-37ei3).
  auto *watcher = new QFutureWatcher<RomHasher::Result>();
  m_inFlightHashes.insert(watcher, cancelToken);
  QObject::connect(watcher, &QFutureWatcher<RomHasher::Result>::finished, watcher,
                   [this, watcher, trimmed, cancelToken, callback = std::move(callback)]() mutable {
                     const RomHasher::Result hashes = watcher->result();
                     m_inFlightHashes.remove(watcher);
                     watcher->deleteLater();
                     if (m_stageReporter) {
                       m_stageReporter(QString());
                     }
                     // Cancel/skip flipped the token mid-hash: resolve cancelled
                     // instead of firing a filename-only jeuInfos request — that
                     // burned one quota request per skipped item, dispatched
                     // AFTER the runner's clearPending already ran.
                     if (cancelToken && cancelToken->load(std::memory_order_acquire)) {
                       callback(ErrorUtils::ErrorContext::error(
                           ErrorUtils::ErrorCode::OperationCancelled,
                           QStringLiteral("Lookup cancelled during ROM hashing"),
                           "ScreenScraperProvider::runLookup"));
                       return;
                     }
                     runLookupAfterHash(trimmed, hashes, std::move(callback));
                   });
  watcher->setFuture(
      QtConcurrent::run([wantInnerHash, filePath, cancelToken]() -> RomHasher::Result {
        auto r = (wantInnerHash && RomHasher::isArchivePath(filePath))
                     ? RomHasher::hashArchiveInnerRom(filePath, cancelToken)
                     // FileHashCache (Kartend-3p42r): skip re-hashing an unchanged
                     // file. Archives stay direct — their inner-ROM hash doesn't
                     // fit the cache's (path,size,mtime)->hash key.
                     : ScreenScraperProvider::hashRegularFileCached(
                           QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
                           filePath, cancelToken);
        if (r.isOk()) {
          return r.value();
        }
        // Kartend-ou0a: surface the failure cause rather than silently
        // falling back to filename-only matching. Without this log the only
        // observable symptom was a missing md5/sha1 in the jeuInfos.php URL
        // — easy to miss, and the next layer's SS response looked superficially
        // normal (just matched the wrong region's ROM record). A cancel/skip
        // is not a hash failure — the continuation resolves it cancelled, so
        // the fallback warning would be a lie.
        if (r.hasErrorCode(ErrorUtils::ErrorCode::OperationCancelled)) {
          return RomHasher::Result{};
        }
        qCWarning(lcScreenScraperProvider).nospace()
            << "ROM hashing failed; jeuInfos.php will fall back to filename-only "
               "matching. This is what produces wrong-region matches when a "
               "symlinked ROM resolves cleanly but Qt's QFile open path didn't "
               "follow it. error='"
            << r.error().message << "' details='" << r.error().details << "' file='" << filePath
            << "'";
        return RomHasher::Result{};
      }));
}

ErrorUtils::Result<RomHasher::Result> ScreenScraperProvider::hashRegularFileCached(
    const QString &mainDbDir, const QString &filePath,
    const std::shared_ptr<std::atomic<bool>> &cancelToken) {
  // Unique connection name per call: the QtConcurrent pool reuses threads and a
  // QSqlDatabase connection is single-thread-affine, so each hash opens (and
  // tears down) its own connection — mirroring ScrapeWriteWorker::openConnection.
  static std::atomic<quint64> seq{0};
  const QString connName = QStringLiteral("kartend_scrape_hashcache_%1")
                               .arg(seq.fetch_add(1, std::memory_order_relaxed));
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    if (DatabaseSchema::openConnection(db, mainDbDir)) {
      // Match the main connection's pragmas (WAL etc.) so this short-lived
      // reader/writer doesn't fight its locking model. The schema itself is
      // owned by DatabaseManager (run at startup), same contract as
      // ScrapeWriteWorker — we never migrate here.
      DatabaseSchema::applyConnectionPragmas(db);
      const ErrorUtils::Result<RomHasher::Result> cached =
          FileHashCache::hashFileCached(db, filePath, cancelToken);
      db.close();
      db = QSqlDatabase();
      QSqlDatabase::removeDatabase(connName);
      return cached;
    }
    db.close();
    db = QSqlDatabase();
  }
  QSqlDatabase::removeDatabase(connName);
  // Cache DB unavailable: degrade to a direct hash so a missing/locked cache
  // never breaks scraping (the FileHashCache acceleration is best-effort).
  return RomHasher::hashFile(filePath, cancelToken);
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
                                  alive = std::weak_ptr<int>(m_lifetimeToken),
                                  callback = std::move(callback)](
                                     const QList<ScreenScraperSystems::System> &systems) mutable {
    // Kartend audit cr950: this systemesListe reply is delivered on the
    // qApp-lifetime HttpClient, which holds this lambda (capturing the provider's
    // `this`) with no QObject connection to sever on teardown. The provider can
    // be destroyed first — e.g. the user cancels a cold-start scrape, the runner
    // finishes and deleteLater-destroys itself, dropping the only shared_ptr to
    // this provider — so guard with a liveness token before touching any member.
    // Still invoke the lookup callback (cancelled) so the batch item resolves
    // instead of hanging, preserving the finished-emission invariant.
    if (alive.expired()) {
      callback(ErrorUtils::ErrorContext::error(
          ErrorUtils::ErrorCode::OperationCancelled,
          QStringLiteral("ScreenScraper provider destroyed during catalog lookup"),
          "ScreenScraperProvider::runLookup"));
      return;
    }
    // Kartend-5l9ow: datCanonicalName is hash-derived — findDatCanonicalName
    // looks the DAT up by this ROM's md5/sha1/crc, so the canonical name
    // belongs to a record whose hash matches the ROM. It is therefore already
    // consistent with the hash sent in the jeuInfos URL below, and SS treats the
    // hash as authoritative regardless, so preferring it over the raw filename
    // cannot steer a hash-confident lookup to a different game.
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
    const QUrl url = ScreenScraperUrls::buildJeuInfosUrl(creds, romnom, systemeid, hashes, hasUser);

    // Kartend-ou0a: filename region detection. The user picks the policy:
    //   TrustScraperFirst: use filename region ONLY when no hash narrowed
    //     the candidate. SS's hash-matched region wins when present.
    //   FilenameWhenAvailable: filename region always wins when detectable
    //     (over SS's matched-ROM region). Use when SS's per-file region
    //     tagging is unreliable.
    //   ScraperOnly: never look at the filename. Pre-fix behaviour.
    const bool haveAnyHash = !hashes.md5.isEmpty() || !hashes.sha1.isEmpty();
    auto regionSource = ScraperOptions::ScraperRegionSource::TrustScraperFirst;
    if (m_settingsAccessor) {
      if (const GeneralSettings *settings = m_settingsAccessor()) {
        regionSource = settings->scraper.options.regionSource;
      }
    }
    QString filenameRegionOverride;
    if (regionSource == ScraperOptions::ScraperRegionSource::FilenameWhenAvailable ||
        (regionSource == ScraperOptions::ScraperRegionSource::TrustScraperFirst && !haveAnyHash)) {
      filenameRegionOverride = ScreenScraperRegion::detectFromFilename(trimmed);
    }

    fetchJeuInfos(url, filenameRegionOverride, std::move(callback));
  });
}

void ScreenScraperProvider::fetchJeuInfos(const QUrl &url, const QString &filenameRegionOverride,
                                          LookupCallback callback) {
  // Kartend-1rtrt retry, hoisted: ProviderBase::getWithRetry owns the bounded
  // transient-retry loop (classification via RetryPolicy::isTransient, tracked
  // per-retry timers); this handler only sees the final Result.
  getWithRetry(userAgentHeader(), url,
               [this, filenameRegionOverride, callback = std::move(callback),
                alive = std::weak_ptr<int>(m_lifetimeToken)](
                   ErrorUtils::Result<QByteArray> response) mutable {
                 // Kartend audit cr950: the provider can be destroyed (cancel during a
                 // scrape) while this jeuInfos reply is in flight on the qApp-lifetime
                 // HttpClient, which holds this lambda's raw `this` with no QObject
                 // connection to sever on teardown. Bail before any member access, but
                 // invoke the callback (cancelled) so the batch item resolves instead of
                 // hanging — the finished-emission invariant.
                 if (alive.expired()) {
                   callback(ErrorUtils::ErrorContext::error(
                       ErrorUtils::ErrorCode::OperationCancelled,
                       QStringLiteral("ScreenScraper provider destroyed during jeuInfos lookup"),
                       "ScreenScraperProvider::fetchJeuInfos"));
                   return;
                 }
                 handleJeuInfosResponse(std::move(response), callback, filenameRegionOverride);
               },
               Scraper::HttpClient::kDefaultMaxResponseBytes,
               // No Content-Type constraint: api2 returns JSON (output=json) but SS
               // also serves text/plain error bodies under 4xx that the parser path
               // surfaces verbatim — pinning "application/json" would mask those.
               QString(),
               // Kartend-8xs72: this URL carries devpassword/sspassword in the query
               // string. Pin the request — and any redirect it follows — to
               // ScreenScraper's domain so a cross-host redirect can't silently forward
               // the credential-bearing URL to an attacker host. Reuses the same
               // allowlist mechanism (hostMatchesAllowlist + UserVerifiedRedirectPolicy)
               // that fetchMediaBytes already applies to response-derived media URLs.
               {QString::fromLatin1(ScreenScraperUrls::SS_MEDIA_HOST_SUFFIX)});
}

void ScreenScraperProvider::handleJeuInfosResponse(ErrorUtils::Result<QByteArray> response,
                                                   const LookupCallback &callback,
                                                   const QString &filenameRegionOverride) {
  if (response.isError()) {
    callback(ScreenScraperUrls::mapScreenScraperHttpError(response.error()));
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
  // keys, media tags, …) can be inspected. Off by default; the file path is
  // logged so it is easy to find.
  //
  // PRIVACY: each response embeds the SS `ssuser` account block (login id,
  // quota counters, membership level). The request URL — which also carries
  // devid/devpassword/ssid/sspassword — is NOT dumped, but treat these files as
  // sensitive. Retention is bounded to the most recent kMaxRetainedScraperDumps
  // so an opt-in debugging session can't accumulate them unbounded; clear the
  // scraper-dump/ directory when done.
  if (qEnvironmentVariableIsSet("KARTEND_SCRAPER_DUMP_JSON")) {
    constexpr int kMaxRetainedScraperDumps = 50;
    const QString dumpDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                .filePath(QStringLiteral("scraper-dump"));
    if (QDir().mkpath(dumpDir)) {
      // Restrict the dump dir to the owner (0700): the payloads embed the
      // ssuser account block, and the default umask would otherwise leave them
      // group/world-readable on a multi-user host (Kartend-jigkr).
      QFile::setPermissions(dumpDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                         QFileDevice::ExeOwner);
      const QString dumpPath = QDir(dumpDir).filePath(
          QStringLiteral("jeuInfos-%1.json").arg(QDateTime::currentMSecsSinceEpoch()));
      QFile dumpFile(dumpPath);
      if (dumpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        dumpFile.write(bytes);
        dumpFile.close();
        // Lock the dump to owner-only (0600), mirroring scrapelogger.cpp — it
        // embeds the ssuser account block (Kartend-jigkr).
        dumpFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        qWarning("[scraper-dump] wrote raw jeuInfos response to %s", qUtf8Printable(dumpPath));
      }
      // Prune oldest dumps beyond the retention cap. The ms-epoch filename is a
      // fixed 13 digits (until year ~2286), so a Name sort is chronological.
      QDir dir(dumpDir);
      const QStringList dumps =
          dir.entryList({QStringLiteral("jeuInfos-*.json")}, QDir::Files, QDir::Name);
      for (int i = 0; i < dumps.size() - kMaxRetainedScraperDumps; ++i) {
        dir.remove(dumps.at(i));
      }
    }
  }
  const QString trimmedHead = QString::fromUtf8(bytes.left(64)).trimmed();
  if (!trimmedHead.startsWith('{') && trimmedHead.startsWith(QLatin1String("Erreur"))) {
    // Server-controlled text headed for details / the always-on error log —
    // flatten to one bounded line (log-injection hardening). Real SS
    // "Erreur ..." bodies are short single-line French sentences.
    callback(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             QStringLiteral("ScreenScraper rejected the request"),
                                             "ScreenScraperProvider")
                 .withDetails(ErrorUtils::sanitizedSingleLine(QString::fromUtf8(bytes), 512)));
    return;
  }
  // Refresh the cached quota from this response's `ssuser` block before
  // parsing the game data. Runs on the "no match" path too (SS still
  // reports the counters there), so the dialog's live readout stays
  // current even for a collection full of unmatched items.
  m_quota.updateFromResponse(bytes);
  // Kartend-399wm: parse the jeuInfos payload exactly once. The full
  // ScrapedItem is the source of truth (cached so fetchDetail() can return
  // it without a second roundtrip); the lightweight candidate the dialog
  // shows is projected from that same item rather than re-parsing the whole
  // document through parseSearchResponse.
  auto parseOpts = buildParseOptions();
  parseOpts.filenameRegionOverride = filenameRegionOverride;
  auto detail = ScreenScraperParser::parseDetailResponse(bytes, parseOpts);
  if (detail.isError()) {
    // "No match" parses to FileNotFound inside the parser; mirror
    // parseSearchResponse's benign-empty contract so the dialog shows "No
    // matches" instead of an error popup, and surface real errors otherwise.
    if (detail.error().code == ErrorUtils::ErrorCode::FileNotFound) {
      callback(QList<Scraper::ScrapeCandidate>{});
    } else {
      callback(detail.error());
    }
    return;
  }
  // Record the editeur/developpeur id→name pairs this response carried —
  // there is no company listing endpoint, so scraped games are the only
  // source of the link that lets a manufacturer parent collection find its
  // company art by name later (Kartend-cnti4).
  recordCompanies(detail.value());
  const Scraper::ScrapeCandidate cand = ScreenScraperParser::candidateFromItem(detail.value());
  const QString id = cand.providerSpecificId;
  if (!id.isEmpty()) {
    m_detailCacheOrder.removeAll(id); // refresh position if re-looked-up
    m_detailCache.insert(id, detail.value());
    m_detailCacheOrder.append(id);
    while (m_detailCacheOrder.size() > kMaxDetailCacheEntries) {
      m_detailCache.remove(m_detailCacheOrder.takeFirst());
    }
  }
  callback(QList<Scraper::ScrapeCandidate>{cand});
}

void ScreenScraperProvider::recordCompanies(const Scraper::ScrapedItem &item) const {
  if (item.publisherId.isEmpty() && item.developerId.isEmpty()) return;
  const QString path = ScreenScraperCompanyRegistry::defaultPath();
  if (path.isEmpty()) return; // no writable cache location — nothing to record into
  if (!m_companyRegistryLoaded) {
    m_companyRegistryLoaded = true;
    // A corrupted file loads as its error; start empty and let the next save
    // rewrite it — losing recorded pairs is recoverable (they re-accumulate
    // on the next scrape), refusing to record forever is not.
    auto loaded = ScreenScraperCompanyRegistry::load(path);
    if (loaded.isOk()) m_companyRegistry = loaded.value();
  }
  bool changed = false;
  if (!item.publisherId.isEmpty()) {
    changed |= ScreenScraperCompanyRegistry::merge(m_companyRegistry, item.publisherId,
                                                   item.publisher);
  }
  if (!item.developerId.isEmpty()) {
    changed |= ScreenScraperCompanyRegistry::merge(m_companyRegistry, item.developerId,
                                                   item.developer);
  }
  if (changed) {
    // Fire-and-forget: a failed write is logged by the cache helper and the
    // in-memory map still serves this session.
    (void)ScreenScraperCompanyRegistry::save(path, m_companyRegistry);
  }
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

// buildJeuInfosUrl + buildSystemeMediaUrl moved to the ScreenScraperUrls
// namespace — see screenscraperurls.{h,cpp}. Pure query construction with
// no provider state, so they live with the other SS wire-shape helpers and
// are unit-tested without a provider instance.

ScreenScraperParser::ParseOptions ScreenScraperProvider::buildParseOptions() const {
  ScreenScraperParser::ParseOptions parseOpts;
  if (m_settingsAccessor) {
    if (const GeneralSettings *settings = m_settingsAccessor()) {
      // mediaMaxDim drives server-side image downscaling on SS — the parser
      // appends it to media URLs when set.
      parseOpts.mediaMaxDim = settings->scraper.options.mediaMaxDimension;
      // JPG output is gated on the user's preset preference — only the
      // Fastest preset honors it (where the bandwidth win outweighs the
      // quality loss). The setting's value is checked here instead of
      // just-the-preset so a Custom user can opt in deliberately.
      parseOpts.preferJpg = settings->scraper.options.preferJpgOutput;
      // Fallback region for region-keyed fields when the matched item's
      // own region has no entry. Each item still honours its own region
      // first inside the parser.
      parseOpts.preferredRegion = settings->scraper.options.preferredScraperRegion;
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
  // ScrapedItem when the id is still in the bounded cache. Keying by id (rather
  // than a single last-lookup slot) means an intervening lookup for another
  // batch item can't displace this one (Kartend-r4tj).
  if (const auto it = m_detailCache.constFind(candidate.providerSpecificId);
      it != m_detailCache.constEnd()) {
    callback(it.value());
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
  // No asset type known here — empty type classifies as Image, keeping the
  // pre-existing conservative image/ pin + image-sized cap.
  fetchMediaBytesForType(url, QString(), std::move(callback));
}

void ScreenScraperProvider::fetchMediaBytesForType(const QUrl &url, const QString &mediaType,
                                                   MediaCallback callback) {
  if (!callback) return;
  // Re-apply the per-host throttle on every media fetch so changes in
  // the Scraper settings panel take effect without restarting. Cheap:
  // HttpClient::setRateLimit just replaces the policy entry.
  registerHostThrottles(m_settingsAccessor ? m_settingsAccessor() : nullptr);
  // Per-kind response guards (Kartend-jjyst.1). The Content-Type prefix keeps
  // the Kartend-9ryx defence — ScreenScraper serves 200-OK text/html "Access
  // denied" / "NOMEDIA" pages under expired or absent media URLs, which the
  // prefix check turns into structured errors instead of decode failures —
  // while matching what the endpoint legitimately serves per kind:
  //   image  → image/*        + the tight image cap (fan-out RAM bound);
  //   video  → video/*        + the wide default cap (videos run tens of MB,
  //            far over kImageMaxResponseBytes);
  //   manual → application/*  + the wide default cap. Manuals are PDFs
  //            (application/pdf) but SS may also serve zip/epub-style docs, so
  //            the broader application/ prefix is checked rather than a single
  //            exact type — it still rejects the text/html error pages.
  const Scraper::MediaKind kind = Scraper::kindForType(mediaType);
  qint64 maxResponseBytes = Scraper::HttpClient::kImageMaxResponseBytes;
  QString expectedContentTypePrefix = QStringLiteral("image/");
  switch (kind) {
  case Scraper::MediaKind::Video:
    maxResponseBytes = Scraper::HttpClient::kDefaultMaxResponseBytes;
    expectedContentTypePrefix = QStringLiteral("video/");
    break;
  case Scraper::MediaKind::Manual:
    maxResponseBytes = Scraper::HttpClient::kDefaultMaxResponseBytes;
    expectedContentTypePrefix = QStringLiteral("application/");
    break;
  case Scraper::MediaKind::Image:
    break;
  }
  Scraper::HttpClient::instance()->get(
      url, userAgentHeader(),
      [callback = std::move(callback)](const ErrorUtils::Result<QByteArray> &response) {
        if (response.isError()) {
          callback(ScreenScraperUrls::mapScreenScraperHttpError(response.error()));
          return;
        }
        callback(response);
      },
      maxResponseBytes, expectedContentTypePrefix,
      // Kartend-pugp.2: url is response-derived; pin it (and its
      // redirects) to ScreenScraper's domain so it can't be steered at
      // an internal https host.
      {QString::fromLatin1(ScreenScraperUrls::SS_MEDIA_HOST_SUFFIX)});
}

// The ScreenScraperProviderHelpers account probes (fetchUserInfo /
// fetchInfraInfo / fetchHealthStatus) and the shared resolveSsCredentials
// moved to screenscraperaccount.{h,cpp} — the settings panel consumes them
// without pulling in this provider TU.
