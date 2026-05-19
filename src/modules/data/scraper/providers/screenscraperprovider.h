#ifndef SCREENSCRAPERPROVIDER_H
#define SCREENSCRAPERPROVIDER_H

#include "datcache.h"
#include "errorutils.h"
#include "metadatalookupprovider.h"
#include "romhasher.h"
#include "screenscrapermediatypecache.h"
#include "screenscraperparser.h"
#include "screenscrapersystems.h"

#include <functional>
#include <optional>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

struct CollectionConfig;
struct GeneralSettings;

namespace ScreenScraperProviderHelpers {

/// Hit SS's `ssuserInfos.php` with whatever credentials are currently
/// in `settings.scraperCredentials["screenscraper"]` (falling back to
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
class ScreenScraperProvider : public MetadataLookupProvider {
public:
  using GeneralSettingsAccessor = std::function<const GeneralSettings *()>;
  /// Resolves the collection context for the next scrape. Returning
  /// nullptr means "no per-collection context available" — provider
  /// falls back to systemeid=0.
  using CollectionAccessor = std::function<const CollectionConfig *()>;

  ScreenScraperProvider(GeneralSettingsAccessor settingsAccessor,
                        CollectionAccessor collectionAccessor);

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
  /// Most recent per-account quota snapshot, parsed from the `ssuser`
  /// block every jeuInfos.php lookup response carries. Stays invalid
  /// (`valid == false`) until the first lookup completes — the batch
  /// driver checks that flag before surfacing the readout. See
  /// `m_lastQuota` for the storage and `updateQuotaFromResponse`.
  [[nodiscard]] Scraper::QuotaStatus quotaStatus() const override { return m_lastQuota; }
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
  void runLookup(const QString &query, const QString &filePath, LookupCallback callback);
  /// Main-thread tail of runLookup, invoked once hashing has finished
  /// (or skipped, when filePath was empty). Reads DAT cache, fires
  /// systems-catalog + jeuInfos requests, threads everything through
  /// the same async chain the original synchronous-hash code used.
  void runLookupAfterHash(const QString &query, const RomHasher::Result &hashes,
                          LookupCallback callback);

  /// Refresh `m_lastQuota` from a raw jeuInfos.php response body. SS
  /// embeds the account's `ssuser` block (request counters + quotas)
  /// in every lookup reply, so this runs once per item with no extra
  /// network cost. A response with no parseable `ssuser` block (e.g.
  /// an anonymous-tier scrape) leaves the previous snapshot intact.
  void updateQuotaFromResponse(const QByteArray &json);

  /// Returns ("dev_id", "dev_password", "ssid", "ss_password") tuple
  /// or empties when not configured. The lookup short-circuits with
  /// a "not configured" error when devid or devpassword are blank;
  /// user creds are optional.
  struct Credentials {
    QString devId;
    QString devPassword;
    QString userId;
    QString userPassword;
  };
  [[nodiscard]] Credentials currentCredentials() const;
  /// Resolves systemeid for the current scrape. Picks the explicit
  /// override on the collection when set; otherwise runs autodetect
  /// against the supplied catalog; otherwise returns 0 (SS's "any
  /// system" sentinel).
  [[nodiscard]] int resolveSystemId(const QList<ScreenScraperSystems::System> &systems) const;
  /// Ensure the systems catalog is loaded — from the disk cache when
  /// fresh, otherwise via a network fetch of systemesListe.php. The
  /// callback fires (on the main thread) once the catalog is ready;
  /// returns an empty list on hard failure so the scrape can still
  /// proceed with systemeid=0.
  using SystemsReadyCallback = std::function<void(QList<ScreenScraperSystems::System>)>;
  void ensureSystemsCatalog(const Credentials &creds, SystemsReadyCallback callback) const;

  GeneralSettingsAccessor m_settingsAccessor;
  CollectionAccessor m_collectionAccessor;
  /// SS's jeuInfos.php returns the candidate AND the full detail in
  /// one response — there's no separate detail endpoint. We cache
  /// the full ScrapedItem during lookup() keyed on the candidate's
  /// providerSpecificId so fetchDetail() returns it without a second
  /// roundtrip. Single-entry cache (overwritten on each lookup) is
  /// enough — the dialog calls fetchDetail right after lookup and
  /// never re-fetches an older candidate.
  mutable QString m_lastDetailId;
  mutable Scraper::ScrapedItem m_lastDetail;

  /// Live per-account quota, refreshed from the `ssuser` block of
  /// every jeuInfos.php response by `updateQuotaFromResponse`.
  /// `quotaStatus()` returns this; it stays invalid until the first
  /// lookup response with an `ssuser` block lands.
  Scraper::QuotaStatus m_lastQuota;

  /// Cached SS media-type catalog (`mediasJeuListe.php`). Populated
  /// lazily — the first scrape kicks off a background fetch via
  /// `ensureMediaTypeCatalog`. The cached map flows into the parser's
  /// ParseOptions so unknown SS tags get friendly labels in the
  /// scrape-result dialog without a Kartend release. Lookup is
  /// canonical-tag → label so the parser only needs the projection.
  mutable QHash<QString, QString> m_mediaTypeLabels;
  /// Triggers a background refresh of the catalog when the on-disk
  /// cache is missing or stale. Cheap (single GET, parsed once per
  /// 30 days). Kicks off after the first credentials become
  /// available; scrapes never block on it.
  void ensureMediaTypeCatalog() const;

  /// Lazily-opened on-disk DAT cache. Initialised the first time a
  /// scrape needs DAT lookup so a user who never configures a DAT
  /// path doesn't pay the sqlite-open cost. Mutable because lookup
  /// is logically const but the cache is an implementation detail.
  mutable std::optional<DatCache::Store> m_datCache;
};

#endif // SCREENSCRAPERPROVIDER_H
