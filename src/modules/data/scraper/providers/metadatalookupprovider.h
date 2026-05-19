#ifndef METADATALOOKUPPROVIDER_H
#define METADATALOOKUPPROVIDER_H

#include <functional>

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
  virtual void fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback callback) = 0;

  /// Download the bytes of a single media asset. Provider-supplied so
  /// each concrete provider can route through its own throttled HTTP
  /// client / auth headers. The result dialog calls this once per
  /// checked MediaAsset on Apply.
  virtual void fetchMediaBytes(const QUrl &url, MediaCallback callback) = 0;

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
};

#endif // METADATALOOKUPPROVIDER_H
