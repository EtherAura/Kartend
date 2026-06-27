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
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

struct CollectionConfig;
struct GeneralSettings;

namespace ScreenScraperProviderHelpers {

/// Hit SS's `ssuserInfos.php` with whatever credentials are currently
/// in `settings.scraper.credentials["screenscraper"]` (falling back to
/// the bundled dev_id for the dev fields when absent). The callback
/// fires on the main thread with the parsed user-info struct or an
/// error context. Used by the Scraper settings panel to surface "this
/// account gets N threads" without forcing the user to scrape first.
using UserInfoCallback =
    std::function<void(ErrorUtils::Result<ScreenScraperParser::ScreenScraperUserInfo>)>;
void fetchUserInfo(const GeneralSettings *settings, UserInfoCallback callback);

/// Hit SS's `ssinfraInfos.php` to get realtime cluster status (CPU
/// load, active scrapers today, anonymous-tier shutdown flags). One
/// cheap probe used by the scrape-result dialog before firing the
/// real lookup so users see "SS is busy right now" instead of a
/// timeout. Needs only dev creds — user creds optional.
using InfraInfoCallback =
    std::function<void(ErrorUtils::Result<ScreenScraperParser::ScreenScraperInfraInfo>)>;
void fetchInfraInfo(const GeneralSettings *settings, InfraInfoCallback callback);

/// Probe ssinfraInfos.php and project the response into a HealthStatus
/// (refuseScrape + humanStatus). Honors `closeforleecher` /
/// `closeforexternalscrapers` for anonymous-only callers. Extracted
/// from ScreenScraperProvider::fetchHealthStatus so the pure
/// transformation (InfraInfo → HealthStatus) can be tested without a
/// network mock. Returns an empty HealthStatus on probe failure —
/// transient blips shouldn't surface in the dialog.
void fetchHealthStatus(const GeneralSettings *settings,
                       MetadataLookupProvider::HealthCallback callback);

} // namespace ScreenScraperProviderHelpers

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
  // Test seam (Kartend-hsboz): buildJeuInfosUrl and the sibling request
  // builders are private but were extracted specifically so the SS query
  // shape can be regression-tested without the network (see the member
  // docs). Granting the test friendship keeps them off the public API while
  // letting test_screenscraperprovider.cpp exercise them directly.
  friend class TestScreenScraperProvider;

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
  [[nodiscard]] QUrl searchUrl(const QString &query) const override;

  void lookup(const QString &query, LookupCallback callback) override;
  /// Context-aware overload: when `ctx.filePath` is set, computes
  /// MD5+SHA1 of the file and passes them (plus romsize) to
  /// jeuInfos.php for hash-based identification — the highest-quality
  /// match path SS supports. Falls back to filename-only when the
  /// path is empty or hashing fails.
  void lookup(const LookupContext &ctx, LookupCallback callback) override;
  void fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback callback) override;
  void fetchMediaBytes(const QUrl &url, MediaCallback callback) override;

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

  /// Issue the jeuInfos.php GET and route the response. Kartend-1rtrt:
  /// on a transient failure (timeout / 5xx / 423) it schedules a bounded,
  /// backoff-paced retry of the same idempotent URL via m_retryTimer
  /// (honouring Retry-After) instead of surfacing the error on the first
  /// attempt; permanent errors and successes fall straight through to
  /// handleJeuInfosResponse. @p attempt is 0 on the initial call.
  void fetchJeuInfos(const QUrl &url, const QString &filenameRegionOverride,
                     LookupCallback callback, int attempt);

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
  /// buildJeuInfosUrl, the URL builders — unchanged.
  using Credentials = ScreenScraperCatalogManager::Credentials;
  [[nodiscard]] Credentials currentCredentials() const;

  /// Build the jeuInfos.php URL from credentials + resolved system id
  /// + hash result. Extracted from runLookupAfterHash so the SS query
  /// shape can be regression-tested without hitting the network.
  [[nodiscard]] QUrl buildJeuInfosUrl(const Credentials &creds, const QString &romnom,
                                      int systemeid, const RomHasher::Result &hashes,
                                      bool hasUser) const;
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
  /// In-flight single-shot timers driving Kartend-1rtrt's transient-failure
  /// retries, one per scheduled retry. Each timer's lambda captures `this` raw;
  /// the destructor stops + deletes every entry so none fires after free.
  ///
  /// Kartend audit vrqzk: this was a SINGLE shared QTimer m_retryTimer on the
  /// (false) "lookups are sequential" assumption — two concurrent lookups both
  /// retrying clobbered each other (stop()+disconnect() dropped the prior
  /// schedule), so the dropped retry never fired and its lookup callback was
  /// lost (same leak class as the per-lookup hash watcher above). One timer per
  /// retry lets concurrent retries coexist.
  QSet<QTimer *> m_inFlightRetryTimers;
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
