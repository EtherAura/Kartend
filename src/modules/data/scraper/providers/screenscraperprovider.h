#ifndef SCREENSCRAPERPROVIDER_H
#define SCREENSCRAPERPROVIDER_H

#include "datcache.h"
#include "errorutils.h"
#include "metadatalookupprovider.h"
#include "providerbase.h"
#include "romhasher.h"
#include "screenscrapercatalogmanager.h"
#include "screenscraperparser.h"
#include "screenscraperquotamanager.h"
#include "screenscrapersystems.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

struct CollectionConfig;
struct GeneralSettings;

/// ScreenScraper.fr API-backed provider for game collections.
///
/// Auth: dual credentials. The dev credentials (devid + devpassword)
/// gate API access at all; user credentials (ssid + sspassword)
/// unlock higher rate limits + per-account features. Both are
/// user-supplied — Kartend ships no bundled keys per the policy in
/// the original scraper umbrella issue.
///
/// systemeid: read at lookup time from the calling collection. The
/// provider takes a CollectionConfig accessor so the registry can
/// build providers without knowing about MainWindow's collection
/// list — the context-menu callsite supplies a closure that resolves
/// the current collection at the moment the user runs the scrape.
/// When the collection's `screenscraperSystemId` is -1, the provider
/// runs the autodetect heuristic over the collection's extensions /
/// name / type. When autodetect also yields -1, the request is sent
/// with systemeid=0 (SS treats this as "search all systems" — lower
/// match quality but doesn't hard-fail).
class ScreenScraperProvider : public ProviderBase {
public:
  using GeneralSettingsAccessor = std::function<const GeneralSettings *()>;
  /// Resolves the collection context for the next scrape. Returning
  /// nullptr means "no per-collection context available" — provider
  /// falls back to systemeid=0.
  using CollectionAccessor = std::function<const CollectionConfig *()>;

  ScreenScraperProvider(GeneralSettingsAccessor settingsAccessor,
                        CollectionAccessor collectionAccessor);
  ~ScreenScraperProvider() override;

  void setStageReporter(StageReporter reporter) override { m_stageReporter = std::move(reporter); }

  [[nodiscard]] QString id() const override { return QStringLiteral("screenscraper"); }
  [[nodiscard]] QString displayName() const override { return QStringLiteral("ScreenScraper.fr"); }
  [[nodiscard]] QStringList categories() const override { return {QStringLiteral("games")}; }
  [[nodiscard]] Capabilities capabilities() const override {
    return Capability::WebSearch | Capability::MetadataLookup | Capability::MediaFetch;
  }
  /// ScreenScraper can scrape Game items and Platform (console/system) art via
  /// its systemesListe.php catalog + mediaSysteme.php media endpoint
  /// (Kartend-ckepd.4).
  [[nodiscard]] QList<Scraper::ScrapeEntityType> supportedEntities() const override {
    return {Scraper::ScrapeEntityType::Game, Scraper::ScrapeEntityType::Platform};
  }
  [[nodiscard]] QUrl searchUrl(const QString &query) const override;

  void lookup(const QString &query, LookupCallback callback) override;
  /// Context-aware overload: when `ctx.filePath` is set, computes
  /// MD5+SHA1 of the file and passes them (plus romsize) to
  /// jeuInfos.php for hash-based identification — the highest-quality
  /// match path SS supports. Falls back to filename-only when the
  /// path is empty or hashing fails.
  void lookup(const LookupContext &ctx, LookupCallback callback) override;
  void fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback callback) override;
  /// URL-only entry point — no asset type known, so it keeps the
  /// conservative image/ guards (delegates to the kind-aware variant
  /// with an empty type).
  void fetchMediaBytes(const QUrl &url, MediaCallback callback) override;
  /// Kind-aware media fetch: selects the expected Content-Type prefix and
  /// response-size cap from kindForType(mediaType) — image/ + the image cap
  /// for artwork, video/ + the wide default cap for video kinds, and
  /// application/ + the wide default cap for manuals — so video/mp4 and
  /// application/pdf payloads stop being rejected by the image/ pin
  /// (Kartend-jjyst.1).
  void fetchMediaBytesForType(const QUrl &url, const QString &mediaType,
                              MediaCallback callback) override;
  /// Scrape PLATFORM-level art (console logo / system illustration) for the
  /// ScreenScraper systemeid carried in `target.identity`. Resolves the system
  /// in the cached systemesListe.php catalog for its display name, then emits
  /// mediaSysteme.php media URLs (downloaded later via fetchMediaBytes; SS's
  /// 404 for an absent media type is non-fatal). Kartend-ckepd.4.
  void fetchEntity(const Scraper::EntityScrapeTarget &target, DetailCallback callback) override;

  /// Hash a regular (non-archive) file through FileHashCache so an unchanged
  /// file (same size+mtime) is not re-hashed on every scrape. Opens its own
  /// thread-affine connection to the main DB (media.db under @p mainDbDir, where
  /// file_hash_cache lives) and tears it down before returning, so it is safe to
  /// call from the QtConcurrent hashing pool. Degrades to a direct
  /// RomHasher::hashFile when the cache DB cannot be opened, so a missing/locked
  /// cache never breaks scraping. Static + `this`-free for that pool-thread
  /// safety (and so it is unit-testable against a temporary DB dir).
  [[nodiscard]] static ErrorUtils::Result<RomHasher::Result>
  hashRegularFileCached(const QString &mainDbDir, const QString &filePath,
                        const std::shared_ptr<std::atomic<bool>> &cancelToken);

  /// Most recent per-account quota snapshot, parsed from the `ssuser`
  /// block every jeuInfos.php lookup response carries. Stays invalid
  /// (`valid == false`) until the first lookup completes — the batch
  /// driver checks that flag before surfacing the readout. State lives
  /// on m_quota (ScreenScraperQuotaManager); see
  /// screenscraperquotamanager.{h,cpp} for the parse step.
  [[nodiscard]] Scraper::QuotaStatus quotaStatus() const override { return m_quota.status(); }
  /// Polls SS's `ssinfraInfos.php` and projects the response into
  /// the provider-agnostic HealthStatus shape the dialog consumes.
  /// Honors `closeforleecher` for anonymous-only callers — the
  /// dialog refuses the scrape outright in that state instead of
  /// burning quota on uniform-401 responses.
  void fetchHealthStatus(HealthCallback callback) override;

private:
  /// Shared lookup worker behind the two public overloads. `filePath`
  /// is empty for the filename-only path; non-empty triggers
  /// hashing before the API request. The hash phase runs on a
  /// QtConcurrent worker thread so the main UI thread doesn't block
  /// on `QProcess::waitForFinished` inside the archive extractor.
  /// Continuation hops back to the main thread via QFutureWatcher.
  void runLookup(const QString &query, const QString &filePath,
                 const std::shared_ptr<std::atomic<bool>> &cancelToken, LookupCallback callback);
  /// Main-thread tail of runLookup, invoked once hashing has finished
  /// (or skipped, when filePath was empty). Reads DAT cache, fires
  /// systems-catalog + jeuInfos requests, threads everything through
  /// the same async chain the original synchronous-hash code used.
  void runLookupAfterHash(const QString &query, const RomHasher::Result &hashes,
                          LookupCallback callback);

  /// Issue the jeuInfos.php GET and route the response. Delegates to
  /// ProviderBase::getWithRetry, so a transient failure (timeout / 5xx /
  /// 423 / Retry-After'd 429) is retried a bounded number of times on the
  /// same idempotent URL (Kartend-1rtrt, loop hoisted to the base by
  /// Kartend-jjyst.10); permanent errors and successes fall straight
  /// through to handleJeuInfosResponse.
  void fetchJeuInfos(const QUrl &url, const QString &filenameRegionOverride,
                     LookupCallback callback);

  /// HTTP-response tail of runLookupAfterHash. Lifted out of the
  /// deeply-nested HttpClient::get lambda so the lookup chain stops
  /// being a 200-line single function. Handles: transport-level
  /// errors via mapScreenScraperHttpError; SS's plain-text "Erreur"
  /// body (HTTP 200 + French error message on auth/quota failure);
  /// optional KARTEND_SCRAPER_DUMP_JSON diagnostic dump; quota
  /// refresh; and the search/detail parse-and-cache step that feeds
  /// fetchDetail() without a second roundtrip.
  void handleJeuInfosResponse(ErrorUtils::Result<QByteArray> response,
                              const LookupCallback &callback,
                              const QString &filenameRegionOverride);

  /// Snapshot the user's current scraper preferences (image cap, JPG
  /// preference, preferred region, UI language) into a parser
  /// ParseOptions struct. Extracted from handleJeuInfosResponse so the
  /// option-derivation can be unit-tested independently of the network
  /// path. Picks up m_mediaTypeLabels for friendly media-tag rendering.
  [[nodiscard]] ScreenScraperParser::ParseOptions buildParseOptions() const;

  /// Resolve the active collection's DAT-canonical name for the given
  /// hashes. Walks `datFilePaths` top-to-bottom, takes the first
  /// matching record, returns its canonical romName. Empty string when
  /// no collection / no DAT configured / no hash match — caller falls
  /// back to the messy filename. Extracted from runLookupAfterHash so
  /// the cache-driven DAT walk is independently testable.
  [[nodiscard]] QString findDatCanonicalName(const RomHasher::Result &hashes);

  /// Returns ("dev_id", "dev_password", "ssid", "ss_password") tuple
  /// or empties when not configured. The lookup short-circuits with
  /// a "not configured" error when devid or devpassword are blank;
  /// user creds are optional. The struct itself is owned by
  /// ScreenScraperCatalogManager (the catalog dance needs the same
  /// shape); the alias keeps the rest of the provider's call sites —
  /// the ScreenScraperUrls builders — unchanged.
  using Credentials = ScreenScraperCatalogManager::Credentials;
  [[nodiscard]] Credentials currentCredentials() const;

  /// Resolves systemeid for the current scrape. Picks the explicit
  /// override on the collection when set; otherwise runs autodetect
  /// against the supplied catalog; otherwise returns 0 (SS's "any
  /// system" sentinel).
  [[nodiscard]] int resolveSystemId(const QList<ScreenScraperSystems::System> &systems) const;

  GeneralSettingsAccessor m_settingsAccessor;
  CollectionAccessor m_collectionAccessor;
  StageReporter m_stageReporter;
  /// In-flight ROM-hash tasks, each keyed by its own per-lookup QFutureWatcher
  /// and mapped to that task's cooperative cancel token. Each hash runs
  /// off-thread (QtConcurrent) and its main-thread continuation touches `this`;
  /// parenting each watcher to the provider severs its continuation on
  /// destruction, and ~ScreenScraperProvider() flips every token then waits on
  /// every future so no task outlives us (Kartend-s1s98 / Kartend-37ei3).
  ///
  /// Kartend audit vrqzk: this was a SINGLE shared watcher + token, on the
  /// (false) assumption that "lookups are sequential". BatchScrapeRunner runs
  /// up to batchItemConcurrency lookups concurrently through one shared
  /// provider, so each new lookup's disconnect()+setFuture() clobbered the
  /// previous in-flight lookup's continuation — its callback was never invoked
  /// and the batch item leaked forever (the scrape hung at <100% and never
  /// emitted finished). Per-lookup watchers let concurrent hashes coexist.
  QHash<QFutureWatcher<RomHasher::Result> *, std::shared_ptr<std::atomic<bool>>> m_inFlightHashes;
  // The Kartend-1rtrt transient-retry timers and the cr950 liveness token
  // used to live here; both moved to ProviderBase with the shared retry loop
  // (Kartend-jjyst.10). The ensureSystemsCatalog / jeuInfos continuations
  // below still guard with weak_ptr(m_lifetimeToken) — now the inherited
  // token — and, when it has expired, invoke their callback with a cancelled
  // error and bail before touching any member (the finished-emission
  // invariant).
  /// SS's jeuInfos.php returns the candidate AND the full detail in one
  /// response — there's no separate detail endpoint. We cache the full
  /// ScrapedItem during lookup() keyed on the candidate's providerSpecificId so
  /// fetchDetail() returns it without a second roundtrip. Kartend-r4tj: this was
  /// a single overwritten entry that relied on the batch runner calling
  /// fetchDetail synchronously before the next item's lookup completed —
  /// unenforced, and a future async hop would make fetchDetail miss/serve the
  /// wrong item. A small bounded id-keyed map removes that ordering dependency;
  /// FIFO eviction keeps it from growing across a large batch.
  static constexpr int kMaxDetailCacheEntries = 64;
  mutable QHash<QString, Scraper::ScrapedItem> m_detailCache;
  mutable QStringList m_detailCacheOrder; // insertion order for FIFO eviction

  /// Live per-account quota, refreshed from the `ssuser` block of
  /// every jeuInfos.php response by m_quota.updateFromResponse().
  /// quotaStatus() forwards to m_quota.status(); it stays invalid
  /// until the first lookup response with an `ssuser` block lands.
  ScreenScraperQuotaManager m_quota;

  /// Fold a parsed item's publisher/developer id+name pairs into the on-disk
  /// company registry (Kartend-cnti4). Lazy-loads the registry once per
  /// provider instance and writes the file ONLY when a pair is new — across a
  /// large batch the ids repeat constantly, so the steady state is zero I/O.
  void recordCompanies(const Scraper::ScrapedItem &item) const;
  mutable QHash<QString, QString> m_companyRegistry;
  mutable bool m_companyRegistryLoaded = false;

  /// Owns the systemesListe + mediasJeuListe catalog dance — disk
  /// cache load, conditional refetch, parse, persist. The provider
  /// forwards ensure*Catalog calls to it and reads back
  /// `mediaTypeLabels()` for the parser's ParseOptions. State (the
  /// media-type QHash) lives entirely in the manager; the network and
  /// error-mapping collaborators are injected so the manager is
  /// testable without a live SS API. See
  /// screenscrapercatalogmanager.{h,cpp}.
  ScreenScraperCatalogManager m_catalog;

  /// Lazily-opened on-disk DAT cache. Initialised the first time a
  /// scrape needs DAT lookup so a user who never configures a DAT
  /// path doesn't pay the sqlite-open cost. Mutable because lookup
  /// is logically const but the cache is an implementation detail.
  mutable std::optional<DatCache::Store> m_datCache;
};

#endif // SCREENSCRAPERPROVIDER_H
