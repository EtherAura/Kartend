#ifndef METADATALOOKUPPROVIDER_H
#define METADATALOOKUPPROVIDER_H

#include <atomic>
#include <functional>
#include <memory>
#include <type_traits>

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

#include "errorutils.h"
#include "metadataprovider.h"
#include "scrapertypes.h"

/// Extends MetadataProvider with the async API surface every Stage-2
/// API-backed scraper implements. Concrete subclasses (MusicBrainz,
/// future ScreenScraper / TMDB / Open Library) only need to implement
/// the three callback-based methods below; the result dialog and any
/// future batch-scrape driver work against this base, never the
/// concrete subclasses.
///
/// Why callbacks instead of QFuture: QFuture<T> in Qt6 requires the
/// task to live on a QThreadPool. Our HTTP layer runs on the main
/// thread (network ops are inherently async via signal/slot), so
/// callbacks are the simpler shape.
class MetadataLookupProvider : public MetadataProvider {
public:
  using LookupCallback = std::function<void(ErrorUtils::Result<QList<Scraper::ScrapeCandidate>>)>;
  using DetailCallback = std::function<void(ErrorUtils::Result<Scraper::ScrapedItem>)>;
  using MediaCallback = std::function<void(ErrorUtils::Result<QByteArray>)>;

  /// Optional richer context for `lookup()`. Carries both the
  /// human-readable query and (when known) the local file path of
  /// the source item — providers that support hash-based ROM
  /// identification (ScreenScraper) read the path from here and
  /// hash the file before issuing the request. Other providers
  /// ignore the path; the existing `lookup(query, cb)` overload
  /// stays the canonical entry point.
  struct LookupContext {
    QString query;
    QString filePath;
    /// Optional cooperative-cancellation flag. When set true mid-lookup
    /// (by the batch driver's cancel()), hash-based providers abort the
    /// in-flight ROM hash / archive extraction promptly — killing the
    /// extractor QProcess — instead of running it to completion on a
    /// worker thread after the user already cancelled. shared_ptr so the
    /// flag outlives the driver if the worker is still mid-hash. Defaulted so
    /// the single-item lookup paths that don't thread cancellation can brace-
    /// init just {query, filePath} without tripping -Wmissing-field-initializers.
    std::shared_ptr<std::atomic<bool>> cancelToken{};
  };

  /// Search the provider for `query`. Returns up to N candidates;
  /// concrete subclasses pick a sensible cap (typically 5–10). The
  /// `query` is the user-facing item name — the provider is responsible
  /// for any per-API normalisation (URL-encoding, tokenisation, etc.).
  virtual void lookup(const QString &query, LookupCallback callback) = 0;

  /// Context-aware lookup. Default forwards to the simpler
  /// `lookup(query, cb)` so existing providers don't need updating;
  /// providers that want the richer context (file path for hashing,
  /// future per-collection hints) override this directly.
  virtual void lookup(const LookupContext &ctx, LookupCallback callback) {
    lookup(ctx.query, std::move(callback));
  }

  /// Re-fetch the full record for a candidate the user picked. Called
  /// after `lookup()` so the search-result candidates can be lean (just
  /// enough to disambiguate) and the heavy-weight detail fetch only
  /// runs once. Some providers' search endpoints already return
  /// everything needed — those can pass through and return the candidate
  /// data verbatim.
  ///
  /// Caching contract with the batch runner: a provider MAY answer this from
  /// an internal cache populated during lookup() — but the cache must be
  /// keyed on `candidate.providerSpecificId` and hold enough entries that an
  /// interleaved lookup for ANOTHER item (the runner pipelines items when
  /// itemConcurrency > 1) cannot displace this candidate's detail between
  /// its lookup completing and fetchDetail arriving. A single-slot
  /// "last lookup" cache violates the contract. Threading: lookup(),
  /// fetchDetail(), and the HTTP completions all run on the main-thread
  /// event loop, so no locking is needed — only id-keyed, sufficient
  /// capacity. (ScreenScraperProvider's 64-entry FIFO detail cache is the
  /// reference implementation.)
  virtual void fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback callback) = 0;

  /// Download the bytes of a single media asset. Provider-supplied so
  /// each concrete provider can route through its own throttled HTTP
  /// client / auth headers. The result dialog calls this once per
  /// checked MediaAsset on Apply.
  virtual void fetchMediaBytes(const QUrl &url, MediaCallback callback) = 0;

  /// Kind-aware variant: @p mediaType is the asset's free-form type tag
  /// (MediaAsset::type, e.g. "front" / "video-normalized" / "manual").
  /// Providers whose response guards vary per media kind — the expected
  /// Content-Type prefix and the response-size cap differ between images,
  /// videos and manuals — override this so video/pdf payloads aren't
  /// rejected by an image/ pin (Kartend-jjyst.1). The default ignores the
  /// type and forwards to the URL-only method, so image-only providers need
  /// no change. Callers that hold the MediaAsset should prefer this entry
  /// point. Deliberately NOT a fetchMediaBytes overload: subclasses override
  /// only one of the pair, which -Woverloaded-virtual (clang -Wall, -Werror
  /// under KARTEND_MAINTENANCE) flags as hiding.
  virtual void fetchMediaBytesForType(const QUrl &url, const QString &mediaType,
                                      MediaCallback callback) {
    Q_UNUSED(mediaType);
    fetchMediaBytes(url, std::move(callback));
  }

  /// Entity-scope counterpart to lookup()+fetchDetail(): fetch metadata + art
  /// for a non-game entity (platform / collection / category) the provider
  /// supports. Reuses the ScrapedItem result shape — its `media` list carries
  /// the entity art, which the persistence layer routes to the right target.
  /// The default reports "not supported" so existing Game-only providers need
  /// no change; providers that advertise the entity type in supportedEntities()
  /// override this. Fleshed out by the entity-scraping sub-tasks
  /// Reuses DetailCallback (same Result<ScrapedItem>).
  ///
  /// By-value DetailCallback (not const-ref): providers that support entity
  /// scraping std::move the callback into async work in their override, so the
  /// signature must stay by-value to match. This default body only reports
  /// "unsupported" and never moves, so the analyzer sees an avoidable copy here
  /// — suppressed, same as setStageReporter below.
  // NOLINTNEXTLINE(performance-unnecessary-value-param)
  virtual void fetchEntity(const Scraper::EntityScrapeTarget &target, DetailCallback callback) {
    Q_UNUSED(target);
    if (callback) {
      callback(ErrorUtils::ErrorContext::error(
          ErrorUtils::ErrorCode::InvalidArgument,
          QStringLiteral("entity scraping not supported by this provider")));
    }
  }

  /// Provider-supplied snapshot of the upstream's realtime health,
  /// surfaced by the scrape-result dialog before the user clicks
  /// Apply.
  /// - `humanStatus` is shown verbatim when non-empty; an empty string
  ///   means "nothing to surface" (the dialog hides the row).
  /// - `refuseScrape` is true when the upstream has explicitly closed
  ///   to this account tier (e.g. SS's `closeforleecher` flag for
  ///   anonymous traffic). The dialog disables the Apply button so
  ///   we don't fire doomed requests.
  /// Default implementation is a noop — providers without a health
  /// endpoint stay quiet. ScreenScraperProvider overrides to query
  /// `ssinfraInfos.php`.
  struct HealthStatus {
    QString humanStatus;
    bool refuseScrape = false;
  };
  using HealthCallback = std::function<void(HealthStatus)>;
  // By-value (not const-ref): ScreenScraperProvider's override std::moves the
  // callback into its async probe, so const-ref here would force a copy in the
  // override. The base default below doesn't move, so the check fires only here.
  // NOLINTNEXTLINE(performance-unnecessary-value-param)
  virtual void fetchHealthStatus(HealthCallback callback) {
    if (callback) callback(HealthStatus{});
  }

  /// Provider's most recent per-account request-quota snapshot. Read
  /// by the batch-scrape driver after each item so the dialog can
  /// surface a live "N / M requests today" readout. The default
  /// returns an invalid (`valid == false`) status — providers without
  /// a quota concept (everything except ScreenScraper today) inherit
  /// it and stay silent. ScreenScraperProvider overrides this to
  /// return the quota parsed from the `ssuser` block every lookup /
  /// detail response carries.
  [[nodiscard]] virtual Scraper::QuotaStatus quotaStatus() const { return {}; }

  /// True when @p err signals THIS provider's request quota is spent — the
  /// scrape drivers then stop dispatching, keep the un-finished work queued,
  /// and persist it as the resume point instead of machine-gunning doomed
  /// requests (which, on ScreenScraper, deepens the failed-lookup ban).
  /// The default covers ScreenScraper's non-standard 430 (daily request
  /// quota) / 431 (daily failed-lookup quota) — unassigned codes no other
  /// integrated provider emits, so inheriting the default is harmless.
  /// RFC 6585's 429 is deliberately NOT quota exhaustion (Kartend-jjyst.3):
  /// realistic 429 sources (TMDB's image CDN, OpenLibrary, Cover Art Archive)
  /// are seconds-long burst limits, and a first 429 halting a whole
  /// multi-collection overnight run stranded it. 429 is handled as transient
  /// throttling instead — RetryPolicy::isTransient waits out a Retry-After
  /// hint, and BatchScrapeRunner escalates to a queue stop only on repeated
  /// consecutive 429s. A provider whose upstream signals quota differently
  /// (custom status, an error-body marker mapped into the ErrorContext)
  /// overrides this; the drivers carry no per-provider status-code knowledge
  /// of their own.
  [[nodiscard]] virtual bool isQuotaExhausted(const ErrorUtils::ErrorContext &err) const {
    return err.httpStatus == 430 || err.httpStatus == 431;
  }

  /// Optional progress reporter for long-running lookup stages
  /// (hashing a multi-GB ROM, extracting an archive, awaiting a slow
  /// API response). The reporter is invoked on the GUI thread with a
  /// short user-facing string ("Hashing ROM…", "Extracting archive…",
  /// "") — empty signals "stage cleared". Default implementation is a
  /// noop; providers that benefit from progress surfacing override.
  /// Kartend-ou0a: ScreenScraperProvider uses this so the batch
  /// progress view doesn't look frozen during the multi-minute PS2
  /// .zip extraction.
  using StageReporter = std::function<void(const QString &stage)>;
  // By-value (not const-ref): ScreenScraperProvider's override std::moves the
  // reporter into a member, so const-ref here would force a copy in the override.
  // NOLINTNEXTLINE(performance-unnecessary-value-param)
  virtual void setStageReporter(StageReporter /*reporter*/) {}
};

// Providers are owned and deleted through a MetadataLookupProvider* (often a
// MetadataProvider*) by the result dialog and batch driver, so the destructor
// must be virtual or that delete is UB. It is — inherited from
// MetadataProvider's `virtual ~MetadataProvider() = default;`. Lock the
// invariant in so dropping that base destructor fails loudly here (Kartend-q9w8q).
static_assert(std::has_virtual_destructor_v<MetadataLookupProvider>,
              "MetadataLookupProvider is deleted polymorphically through base pointers; its "
              "destructor must be virtual.");

#endif // METADATALOOKUPPROVIDER_H
